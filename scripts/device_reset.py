"""Adds two device targets, usable on any env that includes this script:

    pio run -e x4pro -t reset    # hard-reset the board over USB, nothing else
    pio run -e x4pro -t flashall # write bootloader+partitions+otadata+app, then reset

`reset` exists because the X4 Pro has no hardware reset button: its power button
(GPIO3) is a plain input polled by firmware, and the board has no PMIC, so if the
running image is wedged or absent there is no button chord that will reboot it.
esptool can always do it over the S3's built-in USB-Serial-JTAG.

`flashall` exists because a plain `-t upload` writes only the app at 0x10000
(app0), while the X4 Pro's factory otadata selects **app1** - so the board resets
correctly and then boots the stock firmware, making a good flash look like a dead
one. The factory image includes boot_app0.bin at 0xe000, which repoints otadata at
app0 so the freshly written firmware is what actually runs.
"""

import subprocess

Import("env")


# Reset sequence to use before connecting. `default-reset` drives DTR/RTS as if a USB-UART
# bridge had EN/GPIO0 wired to them. The X4 Pro has no bridge — the port is the S3's native
# USB — so that does nothing and esptool reports "No serial data received". `usb-reset` is
# the in-band sequence for native USB and is what this board needs.
def _resetMode(env):
    return "usb-reset" if env["PIOENV"] == "x4pro" else "default-reset"


def _esptool(env, args, label):
    port = env.subst("$UPLOAD_PORT")
    cmd = [env.subst("$PYTHONEXE"), env.subst("$UPLOADER"), "--chip", "esp32s3"]
    if port:
        cmd += ["--port", port]
    else:
        print(f"[{label}] no UPLOAD_PORT set; letting esptool auto-detect the port")
    cmd += args
    print(f"[{label}] {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def device_reset(*_, **__):
    # `chip_id` is the cheapest command that still connects; the reset is the
    # point, and --after hard-reset performs it as esptool exits.
    _esptool(env, ["--before", _resetMode(env), "--after", "hard-reset", "chip_id"], "reset")
    print("[reset] board hard-reset; USB will re-enumerate (port may briefly disappear)")


def device_flashall(*_, **__):
    image = env.subst("$BUILD_DIR/${PROGNAME}.factory.bin")
    _esptool(
        env,
        ["--before", _resetMode(env), "--after", "hard-reset", "write-flash", "0x0", image],
        "flashall",
    )
    print("[flashall] wrote the full factory image at 0x0 and reset; otadata now selects app0")


env.AddCustomTarget(
    "reset", None, device_reset,
    title="Reset device",
    description="Hard-reset the board over USB (no flashing)",
)
env.AddCustomTarget(
    "flashall", "$BUILD_DIR/${PROGNAME}.bin", device_flashall,
    title="Flash full image",
    description="Write bootloader+partitions+otadata+app, then reset",
)

# --- Note on `-t upload` ----------------------------------------------------
# No override is needed here. PlatformIO's FLASH_EXTRA_IMAGES is populated on the
# arduino path too, so a plain `-t upload` already writes:
#     0x0000  bootloader.bin
#     0x8000  partitions.bin
#     0xe000  boot_app0.bin   <- otadata; re-selects app0
#     0x10000 firmware.bin    <- the app
# and UPLOADERFLAGS carries `--after hard-reset`, so the board reboots into the
# firmware it just wrote. An earlier version of this script repointed UPLOADCMD at
# the merged factory image at 0x0; that duplicated the three extra images and made
# esptool fail with "Detected overlap at address: 0x0". Do not reintroduce it.
# `flashall` below remains available for writing the single merged image, and it
# builds its own esptool argv rather than reusing UPLOADERFLAGS, so it cannot clash.
