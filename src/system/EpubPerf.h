#pragma once

// High-frequency EPUB diagnostics are intentionally opt-in. Rendering or parsing
// a chapter can touch hundreds of elements; sending one line per image/transfer
// over the Sticky UART materially changes the timings being measured.
#if defined(ENABLE_EPUB_PERF_LOG)
#define EPUB_PERF_LOG(...) INX_SERIAL.printf(__VA_ARGS__)
#else
#define EPUB_PERF_LOG(...) \
  do {                 \
  } while (0)
#endif

