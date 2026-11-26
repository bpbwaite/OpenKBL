"""Runs the power_draw hardware test from a Siglent SPD1000X bench PSU and logs the draw.

Flash the board with `pio run -e power_draw -t upload`, unplug USB, and wire the board to the
PSU. Then run:

    python scripts/power_draw_log.py [--host 192.168.0.183] [--out power_draw.csv]

The script checks the PSU's setpoints, turns the output on (which boots the board and starts the
test), polls voltage and current until the test's timeline is over, then turns the output off.
It turns the output off and stops early if the current passes CUTOFF_A, if the output drops out,
or on Ctrl-C or any error.

Each CSV row is one sample: time since the output came on, measured V, A and W, and where in the
test's timeline the sample falls. The labels come from the timeline in src/tests/power_draw.cpp,
mirrored below, lined up on the falling edge of the test's sync pulse. The PSU's readings lag and
update only about every 0.37 s; the lag is in the sync edge too, so it cancels out. t_in_step_s lets
an analysis skip the start of each step, where a reading may still show the previous level.

Only the standard library is needed.
"""

from __future__ import annotations

import argparse
import csv
import socket
import sys
import time
from datetime import datetime

CUTOFF_A = 4.9
EXPECTED_V = 5.0
EXPECTED_V_TOLERANCE = 0.1
WRITE_DELAY_S = 0.12  # the SPD1000X drops a command sent straight after another
SOCKET_TIMEOUT_S = 3.0

# The test's timeline; keep in step with src/tests/power_draw.cpp.
STEP_S = 1.0
OFF_S = 10.0
SYNC_S = 2.0
SWEEP_S = 255 * STEP_S
SWEEP_COLORS = ["red", "green", "blue", "white"]

# The sync pulse is lined up by its falling edge. The PSU reads 0 A for about the first 0.4 s
# after turn-on, by which time the board has booted and the pulse is already on, so there is no
# reading from before it to find a rising edge against. Measured: about 0.32 A during the pulse,
# 0.08 A after it.
SYNC_FALL_A = 0.1  # drop below the pulse's peak reading that marks its end
SYNC_MIN_HIGH_S = SYNC_S / 2  # a drop sooner than this after readings start is boot noise, not the pulse
SYNC_WAIT_S = 15.0  # give up if the sync pulse hasn't ended by then
DROPOUT_V = 2.0  # measured voltage below this, after the output came up, means it dropped out
END_MARGIN_S = 5.0


def build_timeline() -> list[tuple[str, str, float]]:
    """(segment, color, duration in s) for each segment, in order."""
    timeline = [("sync", "white", SYNC_S), ("baseline", "", OFF_S)]
    for color in SWEEP_COLORS:
        timeline += [("single", color, SWEEP_S), ("gap", "", OFF_S), ("all", color, SWEEP_S), ("gap", "", OFF_S)]
    return timeline


TIMELINE = build_timeline()
TIMELINE_S = sum(duration for _, _, duration in TIMELINE)


def label(t_sync: float) -> tuple[str, str, int | str, float | str]:
    """(segment, color, level, time into the step) for a time measured from the sync edge."""
    if t_sync < 0:
        return ("boot", "", "", "")
    for segment, color, duration in TIMELINE:
        if t_sync < duration:
            if segment in ("single", "all"):
                return (segment, color, int(t_sync // STEP_S) + 1, round(t_sync % STEP_S, 3))
            return (segment, color, "", round(t_sync, 3))
        t_sync -= duration
    return ("done", "", "", "")


class Psu:
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_S)
        self.reader = self.sock.makefile("rb")

    def write(self, command: str) -> None:
        self.sock.sendall(command.encode("ascii") + b"\n")
        time.sleep(WRITE_DELAY_S)

    def query(self, command: str) -> str:
        self.sock.sendall(command.encode("ascii") + b"\n")
        line = self.reader.readline()
        if not line:
            raise ConnectionError(f"PSU closed the connection during {command!r}")
        return line.decode("ascii").strip()

    def query_float(self, command: str) -> float:
        return float(self.query(command))

    def output_off(self) -> None:
        self.write("OUTPut CH1,OFF")

    def close(self) -> None:
        self.reader.close()
        self.sock.close()


def check_setpoints(psu: Psu) -> None:
    volts = psu.query_float("CH1:VOLTage?")
    amps = psu.query_float("CH1:CURRent?")
    print(f"PSU: {psu.query('*IDN?')}")
    print(f"Setpoints: {volts:.3f} V, current limit {amps:.3f} A")
    if abs(volts - EXPECTED_V) > EXPECTED_V_TOLERANCE:
        raise SystemExit(f"Voltage setpoint is {volts} V, expected {EXPECTED_V} V; not turning the output on")
    if int(psu.query("SYSTem:STATus?"), 16) & 0x10:
        raise SystemExit("The PSU output is already on; turn it off first so the test starts from power-on")


def run(psu: Psu, out_path: str) -> int:
    check_setpoints(psu)
    print(f"Test timeline: {TIMELINE_S / 60:.1f} min after the sync pulse. Logging to {out_path}")

    with open(out_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["t_s", "volts", "amps", "watts", "segment", "color", "level", "t_in_step_s"])

        psu.write("OUTPut:RESEt:PROTect")  # clears a latched trip, which would leave the output at 0 V
        psu.write("OUTPut CH1,ON")
        t_on = time.monotonic()
        print("Output on.")

        t_live = None  # time since output on of the first nonzero current reading
        sync_peak_a = 0.0
        t_sync = None  # time since output on that the sync pulse started, worked out from its end
        peak_a = 0.0
        last_report = None
        while True:
            t = time.monotonic() - t_on
            volts = psu.query_float("MEASure:VOLTage? CH1")
            amps = psu.query_float("MEASure:CURRent? CH1")
            peak_a = max(peak_a, amps)

            if t_sync is None:
                if t_live is None and amps > 0:
                    t_live = t
                if t_live is not None:
                    sync_peak_a = max(sync_peak_a, amps)
                    if amps < sync_peak_a - SYNC_FALL_A and t - t_live >= SYNC_MIN_HIGH_S:
                        t_sync = t - SYNC_S
                        print(f"Sync pulse ended at {t:.2f} s ({sync_peak_a:.3f} A -> {amps:.3f} A). The test ends at about {t_sync + TIMELINE_S:.0f} s.")
                if t_sync is None and t > SYNC_WAIT_S:
                    print(f"No sync pulse within {SYNC_WAIT_S} s (current {amps:.3f} A). Is the board flashed and wired?")
                    return 1

            row_label = label(t - t_sync) if t_sync is not None else ("boot", "", "", "")
            writer.writerow([round(t, 3), volts, amps, round(volts * amps, 4), *row_label])

            if amps > CUTOFF_A:
                psu.output_off()
                print(f"CUT-OFF: {amps:.3f} A > {CUTOFF_A} A at {t:.1f} s ({' '.join(map(str, row_label[:3]))}). Output off.")
                return 2
            if t > 2.0 and volts < DROPOUT_V:
                psu.output_off()
                print(f"Output dropped out ({volts:.3f} V) at {t:.1f} s; a protection trip? Output off.")
                return 3

            if row_label[:2] != last_report:
                last_report = row_label[:2]
                print(f"[{t:7.1f} s] {row_label[0]} {row_label[1]}".rstrip() + f"  ({amps:.3f} A)")
                f.flush()

            if t_sync is not None and t - t_sync > TIMELINE_S + END_MARGIN_S:
                psu.output_off()
                print(f"Test done. Peak {peak_a:.3f} A. Output off.")
                return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="192.168.0.183")
    parser.add_argument("--port", type=int, default=5025)
    parser.add_argument("--out", default=f"power_draw_{datetime.now():%Y%m%d_%H%M%S}.csv")
    args = parser.parse_args()

    psu = Psu(args.host, args.port)
    try:
        return run(psu, args.out)
    finally:
        # whatever happened, leave the output off
        try:
            psu.output_off()
            print("Output confirmed off." if not int(psu.query("SYSTem:STATus?"), 16) & 0x10 else "WARNING: output still reports on!")
        finally:
            psu.close()


if __name__ == "__main__":
    sys.exit(main())
