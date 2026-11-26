#pragma once

#include <Arduino.h>

#include "app_conf.h"
#include "demo_mode.h"
#include "hardware.h"
#include "midi_util.h"
#include "pitch_class.h"
#include "settings_menu.h"

// Typed MIDI test commands over USB serial, for testing without a piano.
// Enable with -DKBL_USB_MIDI_DEBUG in platformio.ini. Type `help` for commands.

#ifdef KBL_USB_MIDI_DEBUG

// defined in main.cpp
void handleNoteOn(byte channel, byte note, byte velocity);
void handleNoteOff(byte channel, byte note, byte velocity);
void handleControlChange(byte channel, byte number, byte value);
void setMasterBrightness(int level);
void setBaseHue(uint8_t hue);

namespace okbl
{
    namespace serialcmd
    {
        static const byte CHANNEL         = 1; // handlers ignore it
        static const uint8_t NOTE_LOWEST  = keys::MIDI_NOTE_LOWEST;
        static const uint8_t NOTE_HIGHEST = keys::MIDI_NOTE_HIGHEST;
        static const byte CC_SUSTAIN      = midi::CC_SUSTAIN;

        inline bool isNumber(const char *s) {
            if (!*s)
                return false;
            for (; *s; s++) {
                if (!isdigit((unsigned char) *s))
                    return false;
            }
            return true;
        }

        // scientific pitch notation: C4 = 60, F#3, Bb2. returns -1 if invalid or off the keyboard
        inline int parseNote(const char *s) {
            int pc;
            switch (toupper((unsigned char) s[0])) {
            case 'C':
                pc = pitch::C;
                break;
            case 'D':
                pc = pitch::D;
                break;
            case 'E':
                pc = pitch::E;
                break;
            case 'F':
                pc = pitch::F;
                break;
            case 'G':
                pc = pitch::G;
                break;
            case 'A':
                pc = pitch::A;
                break;
            case 'B':
                pc = pitch::B;
                break;
            default:
                return -1;
            }
            const char *p = s + 1;
            if (*p == '#') {
                pc++;
                p++;
            }
            else if (*p == 'b' || *p == 'B') {
                pc--;
                p++;
            }

            if (!isNumber(p))
                return -1;
            int note = (atoi(p) + 1) * 12 + pc;
            if (note < NOTE_LOWEST || note > NOTE_HIGHEST)
                return -1;
            return note;
        }

        inline void printHelp() {
            debugf("USB MIDI commands:\n");
            debugf("  on <note>... [vel]   e.g. on C4, on C4 E4 G4 80\n");
            debugf("  off <note>...        e.g. off C4 E4 G4\n");
            debugf("  sus on|off           sustain pedal\n");
            debugf("  release              all keys up, sustain off\n");
            debugf("  set <setting> <opt>  e.g. set color keycolors. Settings and options:\n");
            for (size_t g = 0; g < menu::GROUP_COUNT; g++) {
                const menu::MenuGroup &group = menu::GROUPS[g];
                if (group.kind == menu::GroupKind::Action)
                    continue;
                debugf("    %-12s", group.name);
                for (uint8_t o = 0; o < group.optionCount; o++)
                    debugf(" %s", group.options[o].name);
                debugf("\n");
            }
            debugf("  settings             print the current settings\n");
            debugf("  menu                 open/close the Settings Menu, like the encoder switch\n");
            debugf("  bright <1-%d>        master brightness\n", lights::MASTER_BRIGHTNESS_STEPS);
            debugf("  hue <0-255>          base hue, for color basehue\n");
            debugf("  demo                 start demo mode; on <note> or sus on stops it\n");
            debugf("  help\n");
        }

        // on/off: all notes are validated before any are applied
        inline void cmdNotes(bool on, char **args, size_t argc) {
            uint8_t velocity = DEFAULT_VELOCITY;
            if (on && argc > 0 && isNumber(args[argc - 1])) {
                velocity = constrain(atoi(args[argc - 1]), 1, 127);
                argc--;
            }
            if (argc == 0) {
                debugf("? no notes given\n");
                return;
            }

            int notes[MAX_NOTES];
            for (size_t i = 0; i < argc; i++) {
                notes[i] = parseNote(args[i]);
                if (notes[i] < 0) {
                    debugf("? bad note '%s' (use A0..C8, e.g. C4, F#3, Bb2)\n", args[i]);
                    return;
                }
            }

            debugf("> %s", on ? "on" : "off");
            for (size_t i = 0; i < argc; i++) {
                debugf(" %s(%d)", args[i], notes[i]);
            }
            if (on)
                debugf(" vel %d", velocity);
            debugf("\n");

            for (size_t i = 0; i < argc; i++) {
                if (on)
                    handleNoteOn(CHANNEL, notes[i], velocity);
                else
                    handleNoteOff(CHANNEL, notes[i], 0);
            }
        }

        inline void cmdSustain(char **args, size_t argc) {
            if (argc == 1 && (strcasecmp(args[0], "on") == 0 || strcasecmp(args[0], "off") == 0)) {
                bool down = strcasecmp(args[0], "on") == 0;
                debugf("> sus %s\n", down ? "on" : "off");
                handleControlChange(CHANNEL, CC_SUSTAIN, down ? 127 : 0);
            }
            else {
                debugf("? usage: sus on|off\n");
            }
        }

        inline void cmdRelease() {
            debugf("> release\n");
            for (int note = NOTE_LOWEST; note <= NOTE_HIGHEST; note++) {
                handleNoteOff(CHANNEL, note, 0);
            }
            // clears anything the note-offs just marked as sustained
            handleControlChange(CHANNEL, CC_SUSTAIN, 0);
        }

        // Option index for `name` in a Radio group (case-insensitive); -1 if unknown
        inline int parseOption(const menu::MenuGroup &group, const char *name) {
            for (uint8_t o = 0; o < group.optionCount; o++) {
                if (strcasecmp(name, group.options[o].name) == 0)
                    return o;
            }
            return -1;
        }

        inline void cmdSet(char **args, size_t argc) {
            if (argc != 2) {
                debugf("? usage: set <setting> <option> (type help for the list)\n");
                return;
            }
            const menu::MenuGroup *group = menu::findGroup(args[0]);
            if (!group || group->kind == menu::GroupKind::Action) {
                debugf("? unknown setting '%s' (type help for the list)\n", args[0]);
                return;
            }
            int option = parseOption(*group, args[1]);
            if (option < 0) {
                debugf("? unknown option '%s' for %s (type help for the list)\n", args[1], group->name);
                return;
            }
            debugf("> set %s %s\n", group->name, args[1]);
            menu::setValue(*group, option);
        }

        inline void cmdBright(char **args, size_t argc) {
            if (argc == 1 && isNumber(args[0])) {
                debugf("> bright %s\n", args[0]);
                setMasterBrightness(atoi(args[0]));
            }
            else {
                debugf("? usage: bright <1-%d>\n", lights::MASTER_BRIGHTNESS_STEPS);
            }
        }

        inline void cmdHue(char **args, size_t argc) {
            if (argc == 1 && isNumber(args[0]) && atoi(args[0]) <= 255) {
                debugf("> hue %s\n", args[0]);
                setBaseHue(atoi(args[0]));
            }
            else {
                debugf("? usage: hue <0-255>\n");
            }
        }

        inline void runLine(char *line) {
            char *words[MAX_NOTES + 2];
            size_t count = 0;
            for (char *w = strtok(line, " \t"); w && count < MAX_NOTES + 2; w = strtok(nullptr, " \t")) {
                words[count++] = w;
            }
            if (count == 0)
                return;
            if (strtok(nullptr, " \t")) {
                debugf("? too many words\n");
                return;
            }

            const char *cmd = words[0];
            char **args     = words + 1;
            size_t argc     = count - 1;

            if (strcasecmp(cmd, "on") == 0)
                cmdNotes(true, args, argc);
            else if (strcasecmp(cmd, "off") == 0)
                cmdNotes(false, args, argc);
            else if (strcasecmp(cmd, "sus") == 0)
                cmdSustain(args, argc);
            else if (strcasecmp(cmd, "release") == 0)
                cmdRelease();
            else if (strcasecmp(cmd, "set") == 0)
                cmdSet(args, argc);
            else if (strcasecmp(cmd, "settings") == 0)
                menu::printSettings(currentSettings());
            else if (strcasecmp(cmd, "menu") == 0)
                menu::toggle();
            else if (strcasecmp(cmd, "bright") == 0)
                cmdBright(args, argc);
            else if (strcasecmp(cmd, "hue") == 0)
                cmdHue(args, argc);
            else if (strcasecmp(cmd, "demo") == 0)
                demo::start();
            else if (strcasecmp(cmd, "help") == 0)
                printHelp();
            else
                debugf("? unknown command '%s' (type help)\n", cmd);
        }
    } // namespace serialcmd
} // namespace okbl

inline void serialCommandsBegin() {
    okbl::serialcmd::printHelp();
}

// non-blocking; call every loop
inline void serialCommandsPoll() {
    static char line[okbl::serialcmd::MAX_LINE_CHARS + 1];
    static size_t len    = 0;
    static bool overflow = false;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (overflow) {
                debugf("? line too long (max %d chars)\n", okbl::serialcmd::MAX_LINE_CHARS);
            }
            else if (len > 0) {
                line[len] = '\0';
                okbl::serialcmd::runLine(line);
            }
            len      = 0;
            overflow = false;
        }
        else if (len < okbl::serialcmd::MAX_LINE_CHARS) {
            line[len++] = c;
        }
        else {
            overflow = true;
        }
    }
}

#else

inline void serialCommandsBegin() {}
inline void serialCommandsPoll() {}

#endif
