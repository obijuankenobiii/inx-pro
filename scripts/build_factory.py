"""Build the single-file factory image used by the manufacturer web uploader."""

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
    # Flash size comes from the active board (Sticky 32MB, X4 Pro 16MB) rather
    # than a constant, so merge_bin lays the image out for the right part.
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")

    files = (app, bootloader, partitions, boot_app)
    if not all(path.is_file() for path in files):
        missing = ", ".join(str(path) for path in files if not path.is_file())
        raise RuntimeError(f"Cannot build factory image; missing: {missing}")

    # Use the uploader selected by PlatformIO. The pioarduino platform ships
    # its own esptool entry point; resolving the package directory directly can
    # pick up an older global esptool.py and fail on its newer rich_click API.
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
        flash_size,
        "0x0",
        str(bootloader),
        "0x8000",
        str(partitions),
        "0xe000",
        str(boot_app),
        "0x10000",
        str(app),
    ]

    print(f"Building web uploader image: {output} (flash_size={flash_size})")
    subprocess.run(command, check=True)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_factory)
