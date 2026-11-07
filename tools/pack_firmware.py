# After the app image is linked, publish two files:
#   firmware.ota.bin  — app only, upload this from the device OTA page
#   firmware.full.bin — bootloader + partition table + otadata + app, flash at 0x0
Import("env")

import os
import shutil


def pack_firmware(source, target, env):
    build = env.subst("$BUILD_DIR")
    fw = os.path.join(build, "firmware.bin")
    ota = os.path.join(build, "firmware.ota.bin")
    full = os.path.join(build, "firmware.full.bin")
    shutil.copyfile(fw, ota)

    framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0 = os.path.join(framework, "tools", "partitions", "boot_app0.bin")
    bootloader = os.path.join(build, "bootloader.bin")
    parts = os.path.join(build, "partitions.bin")
    python = env.subst("$PYTHONEXE")

    cmd = [
        f'"{python}"',
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "merge-bin",
        "-o",
        f'"{full}"',
        "--flash-mode",
        "dio",
        "--flash-freq",
        "80m",
        "--flash-size",
        "16MB",
        "0x0",
        f'"{bootloader}"',
        "0x8000",
        f'"{parts}"',
        "0xe000",
        f'"{boot_app0}"',
        "0x10000",
        f'"{fw}"',
    ]
    rc = env.Execute(" ".join(cmd))
    if rc:
        print("merge_bin failed:", rc)
        return
    print("OTA image :", ota)
    print("Full image:", full)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", pack_firmware)
