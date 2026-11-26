# Post-build script for the release env: writes the esptool images to dist/, named with
# `version` from [common] in platformio.ini:
#   okbl-firmware-<version>-merged.bin  flash at 0x0 (bootloader, partitions, boot_app0, app)
#   okbl-firmware-<version>-app.bin     flash at 0x10000, onto a board that already has the rest
# The offsets and flash settings come from the espressif32 builder, so they match `-t upload`.

import os
import shutil

Import("env")


def write_dist(source, target, env):
    version = env.GetProjectConfig().get("common", "version")
    dist = os.path.join(env.subst("$PROJECT_DIR"), "dist")
    os.makedirs(dist, exist_ok=True)
    name = os.path.join(dist, "okbl-firmware-" + version)
    app = target[0].get_abspath()

    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, '"%s"' % env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), '"%s"' % app]

    merge = [
        '"%s"' % env.subst("$UPLOADER"),
        "--chip", env.subst("$BOARD_MCU"),
        "merge-bin", "-o", '"%s-merged.bin"' % name,
        "--flash-mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash-freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash-size", env.BoardConfig().get("upload.flash_size", "4MB"),
    ] + images
    if env.Execute(" ".join(merge)):
        env.Exit(1)
    shutil.copyfile(app, name + "-app.bin")
    print("Wrote %s-merged.bin and %s-app.bin" % (name, name))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_dist)
