#pragma once

#include <cstdint>
#include <string>

/** One highlight row saved in an ANN3 annotation shard. */
struct EpubAnnotationRecord {
  uint32_t timestamp = 0;
  std::string text;
  uint16_t startSpine = 0xFFFF;
  uint16_t startPage = 0;
  uint16_t endSpine = 0xFFFF;
  uint16_t endPage = 0xFFFF;
  uint16_t pageWordLo = 0xFFFF;
  uint16_t pageWordHi = 0xFFFF;
  uint16_t startPageWordLo = 0xFFFF;
  uint16_t startPageWordHi = 0xFFFF;
  /** Optional recorded voice-note WAV. Transcription is stored separately in note. */
  std::string noteAudioPath;
  /** Optional voice/text note attached to this highlight. */
  std::string note;
};
