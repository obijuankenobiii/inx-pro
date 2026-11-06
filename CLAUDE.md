# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Inx Pro is C++ (gnu++2a) Arduino/ESP-IDF firmware for ESP32-S3 e-paper readers: the Seeed reTerminal Sticky and the Xteink X4 Pro (both SPI-controller 800x480 panels), plus an experimental port to the LilyGo T5 S3 E-Paper Pro / Pro Lite (raw-parallel ED047TC1, 960x540). It is built with PlatformIO. It started as a fork of CrossPoint but no longer pulls changes from it, so the simulator and some macros still use `CROSSPOINT_*` names.

Hardware access goes through the `freeink-sdk` git submodule. Leave it unmodified (don't hand-edit it). If the folder is empty, run `git submodule update --init --recursive`.

## Commands

```sh
pio run -e sticky                     # default env (default_envs = sticky)
pio run -e x4pro
pio run -e t5s3
pio run -e sticky -t upload
pio run -e x4pro -t flashall --upload-port /dev/...   # full image: bootloader + partitions + OTA + app
pio run -e x4pro -t upload --upload-port /dev/...     # app only
pio device monitor -e sticky
python3 scripts/debugging_monitor.py [port]           # needs pyserial colorama matplotlib

# Native simulators (need SDL2). The SD card is mapped to the folder in CROSSPOINT_SIM_SD.
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator -t run_simulator       # full device UI
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator_web -t run_simulator   # web dashboard only, http://127.0.0.1:8080/
```

CI (`.github/workflows/ci.yml`) runs these checks. Run them before you consider a change done:

```sh
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high   # cppcheck
bin/clang-format-fix          # formats every tracked C/C++ file; -g formats only modified files. CI uses clang-format 21.
pio run
```

PR titles must follow Conventional Commits (`feat:`, `fix:`, `chore:` …). A semantic-PR check enforces this.

### Tests

The only automated test is a standalone hyphenation evaluation. It builds with the host `c++`, not PlatformIO:

```sh
test/run_hyphenation_eval.sh [args]   # compiles into build/hyphenation_eval/ and runs it
```

## Architecture

### Several targets, one codebase

`platformio.ini` puts all shared settings in `[s3_common]`. Each device env overrides only the board, flash size, partition table, `-DFREEINK_DEVICE_STICKY` / `-DFREEINK_DEVICE_X4PRO` / `-DFREEINK_DEVICE_LILYGO`, USB/serial setup, and a few extra libraries:
- **HAL selection:** `lib/hal_sticky`, `lib/hal_x4pro` and `lib/hal_t5s3` export the same `HalDisplay.h` / `HalGPIO.h` interface. Each env uses `lib_ignore` to hide the others, so only one is ever visible. A new HAL must be added to every other env's `lib_ignore`.
- **Per-device sources:** `src/system/TouchOrientation{Sticky,X4Pro,T5S3}.cpp` are excluded in common and re-added per env. The X4 Pro and T5 S3 also exclude `VoiceNoteActivity.cpp` because they have no microphone. The per-device image tone tables live in each HAL's `ImageToneMap.cpp`.
- **Device code in shared files:** use the device macros, the `FREEINK_CAP_*` capability macros from freeink-sdk's `BoardConfig.h`, or app-level feature flags set in the env's `build_flags`, such as `INX_HAS_FRONTLIGHT`. Prefer a feature flag over `FREEINK_DEVICE_X4PRO` when the code is about a feature rather than the X4 Pro's panel. The `FREEINK_DEVICE_X4PRO ? 20 : 15`-style layout offsets are X4 Pro bezel-specific. Sticky-only peripherals are the mic, IMU, and temp/humidity sensor. X4 Pro-only peripherals are the frontlight and its SDMMC SD card. The X4 Pro uses 1-bit SDMMC, not SPI, so it needs `USE_BLOCK_DEVICE_INTERFACE`.
- **Serial:** log through the `INX_SERIAL` macro, not hard-coded `Serial`/`Serial0`. Read the USB comments in `platformio.ini` before changing `ARDUINO_USB_*`. Some settings there were tried and reverted because they made the X4 Pro unflashable or broke the Sticky mic.
- **Flash size:** the X4 Pro and T5 S3 app partitions are 7.87 MB (builds are about 4.9 MB as of the T5 port). Watch binary growth.
- **T5 S3 display:** freeink-sdk's `LgfxEpdDriver` wraps LovyanGFX from M5GFX. M5GFX bundles miniz deflate symbols that clash with `lib/miniz`, so `scripts/m5gfx_miniz_prefix.py` renames them for M5GFX sources only. Grey levels are an overlay on the last black-and-white push, which is why the T5 tone codes differ from the other boards (see `lib/hal_t5s3/ImageToneMap.cpp`).
- **Panel size:** `HalDisplay::DISPLAY_WIDTH` / `DISPLAY_HEIGHT` are the SDK's legacy 800x480 constants. Use `renderer.getPanelWidth()` / `getPanelHeight()` or the display's runtime getters instead.
- **Simulator:** `src/simulator/` holds the shims for the native envs (`-DSIMULATOR`, `SimulatorCompat.h`). The `simulator_web` env compiles only an explicit allow-list of sources.

### Build-time code generation

`scripts/build_html.py` is a `pre:` script. It minifies `src/network/html/*.html` and embeds `data/js/*.js` into PROGMEM headers named `*.generated.h`. These headers are gitignored. Edit the source HTML/JS files, never the generated headers. `scripts/build_factory.py` writes `.pio/build/<env>/firmware.factory.bin`.

Other generators:
- `scripts/generate_hyphenation_trie.py` produces `lib/Epub/Epub/hyphenation/generated/hyph-*.trie.h`. The format is described in `docs/hyphenation-trie-format.md`.
- `fontcon/fontconvert.py` and `scripts/regenerate_bundled_fonts.py` generate the bundled fonts.

### Activities (UI)

Every screen subclasses `Activity` (`src/activity/Activity.h`) and implements `onEnter`, `loop`, and `onExit`. `ActivityWithSubactivity` hosts nested screens. `src/main.cpp` owns the global `GfxRenderer`, `MappedInputManager`, and a single `currentActivity`. Navigation happens through `onGoTo*` / `open*` callback functions that call `switchTo<T>(...)`.

`switchTo` **defers** the switch when it is called from inside an activity's `loop()`. It stores the arguments and runs the switch at the end of `main.cpp`'s `loop()`, so the caller is never deleted while its own code is still running. New navigation should go through `switchTo` and must not delete activities directly.

Activities are grouped by role:
- `page/`: home, library, settings, and stats tabs.
- `reader/`: EPUB, TXT, XTC, PDF, and image readers. The EPUB reader is split into many `*Ui` / feature files under `reader/Epub/`.
- `settings/`, `network/`, `system/`: boot and sleep screens are in `system/`.

### Libraries (`lib/`)

- `Epub`: EPUB support. The OPF, NCX/NAV, and CSS parsers use the vendored `expat`. `ChapterHtmlSlimParser` lays out HTML into `Page`s. Each spine item becomes a `Section` that is cached to SD.
- Other formats: `Txt`, `Xtc`, `Pdf`, `Mobi`.
- Rendering: `GfxRenderer` and `EpdFont`.
- Image decoding: JPEG/PNG to BMP converters, `picojpeg`, `toojpeg`, and stb.
- Archives: `ZipFile`, `miniz`, `uzlib`, `InflateReader`.
- `Serialization`: binary cache files.
- `KOReaderSync`, `OpdsParser`, and `lua` (plugins).

### State and SD-card cache

`src/state/` holds the persisted settings and per-book data: `SystemSetting`, `ReaderSetting`, `BookProgress`, `Statistics`, and `RecentBooks`.

Firmware caches heavy work on the SD card instead of in RAM:
- `/.metadata/`: EPUB metadata, layout sections, progress, and annotations.
- `/.metadata/xtc/`: XTC data.
- `/.system/cache/`: display and image cache.
- `/.system/`: settings.

Large buffers go in the 8 MB octal PSRAM (see `PsramAllocator.h`; framebuffers use `FREEINK_FB_PSRAM`). SD access from multiple tasks is guarded by `util/SdIoMutex`.

If you change a cached binary format, check the documented layouts and version numbers in `docs/file-formats.md` (for example `book.bin` v3), and bump the version so stale caches are rebuilt.

### Web server

`src/network/LocalServer.cpp` serves the file manager and settings UI. `docs/webserver-endpoints.md` documents the endpoints.

## Conventions

- Formatting follows `.clang-format`. Files start with a Doxygen `@file` / `@brief` banner (`scripts/add_doxygen_file_banners.py`), and comments are mostly Doxygen style.
- The version string lives in `[inx] version` in `platformio.ini` and reaches the code as `INX_VERSION`.
