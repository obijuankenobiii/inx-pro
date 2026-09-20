"""Keep Arduino Stream timeouts from busy-spinning and starving the ESP32 idle task.

The Arduino WebServer multipart parser uses Stream::readStringUntil(). On an
incomplete network packet, Stream::timedRead()/timedPeek() poll continuously
until their 5 second client timeout expires. That can trigger the task watchdog
while a browser is uploading a file. Yield one tick on an empty read so the
idle task and networking stack can run while Stream waits for the next byte.
"""

from pathlib import Path

Import("env")


framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
stream_cpp = framework / "cores" / "esp32" / "Stream.cpp"
if not stream_cpp.is_file():
    raise RuntimeError(f"Arduino Stream source not found: {stream_cpp}")

source = stream_cpp.read_text(encoding="utf-8")
for operation in ("read", "peek"):
    original = (
        f"  do {{\n"
        f"    c = {operation}();\n"
        f"    if (c >= 0) {{\n"
        f"      return c;\n"
        f"    }}\n"
        f"  }} while (millis() - _startMillis < _timeout);"
    )
    patched = original.replace(
        "    }\n  } while",
        "    }\n"
        "    delay(1);  // Let the ESP32 idle task run while waiting for stream data.\n"
        "  } while",
    )
    if patched in source:
        continue
    if source.count(original) != 1:
        raise RuntimeError(f"Expected one Stream::{operation} timeout loop in {stream_cpp}")
    source = source.replace(original, patched, 1)

stream_cpp.write_text(source, encoding="utf-8")
print(f"Patched Arduino Stream timeout waits: {stream_cpp}")
