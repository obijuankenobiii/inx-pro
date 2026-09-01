#pragma once

#if defined(ENABLE_EPUB_PERF_LOG)
#define EPUB_PERF_LOG(...) INX_SERIAL.printf(__VA_ARGS__)
#else
#define EPUB_PERF_LOG(...) \
  do {                 \
  } while (0)
#endif

