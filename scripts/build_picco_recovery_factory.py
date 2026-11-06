"""Build the Picco recovery-compatible merged image.

The regular factory script places the application at 0x10000. The BOOX Picco
layout places its ota_0 application at 0xE0000 and otadata at 0x15000, so this
target owns a separate merge step instead of changing the shared script.
"""

from pathlib import Path
import subprocess

Import("env")


def build_factory(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    name = env.subst("$PROGNAME")
    app = build / f"{name}.bin"
    bootloader = build / "bootloader.bin"
    partitions = build / "partitions.bin"
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    boot_app = framework / "tools" / "partitions" / "boot_app0.bin"
    output = build / f"{name}.factory.bin"

    files = (app, bootloader, partitions, boot_app)
    if not all(path.is_file() for path in files):
        missing = ", ".join(str(path) for path in files if not path.is_file())
        raise RuntimeError(f"Cannot build Picco recovery image; missing: {missing}")

    tool = Path(env.subst("$UPLOADER"))
    command = [
        str(tool),
        "--chip",
        "esp32s3",
        "merge_bin",
        "--output",
        str(output),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        env.BoardConfig().get("upload.flash_size", "16MB"),
        "0x0",
        str(bootloader),
        "0x8000",
        str(partitions),
        "0x15000",
        str(boot_app),
        "0xE0000",
        str(app),
    ]
    print(f"Building Picco recovery image: {output}")
    subprocess.run(command, check=True)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_factory)
