/**
 * @file ZipFile.cpp
 * @brief Definitions for ZipFile.
 */

#include "ZipFile.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <miniz.h>

#include <algorithm>
#include <cstring>

#include "../../src/system/EpubPerf.h"

namespace {
constexpr size_t kMinimumStreamChunkSize = 16 * 1024;
constexpr size_t kZipServiceByteBudget = 64 * 1024;
constexpr uint32_t kZipServiceTimeBudgetMs = 8;

// ZIP processing must yield often enough for the Sticky watchdog, but yielding
// around every 1 KiB transfer turns SD throughput into deliberate sleeps. This
// services the watchdog by time or byte budget instead.
class ZipServiceBudget {
 public:
  void account(const size_t bytes) {
    bytesSinceService_ += bytes;
    const uint32_t now = millis();
    if (bytesSinceService_ < kZipServiceByteBudget && now - lastServiceAt_ < kZipServiceTimeBudgetMs) return;
    esp_task_wdt_reset();
    delay(1);
    bytesSinceService_ = 0;
    lastServiceAt_ = millis();
  }

 private:
  size_t bytesSinceService_ = 0;
  uint32_t lastServiceAt_ = millis();
};
}  // namespace

bool inflateOneShot(const uint8_t* inputBuf, const size_t deflatedSize, uint8_t* outputBuf, const size_t inflatedSize) {
  const auto inflator = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
  if (!inflator) {
    INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for inflator\n", millis());
    return false;
  }
  memset(inflator, 0, sizeof(tinfl_decompressor));
  tinfl_init(inflator);

  size_t inBytes = deflatedSize;
  size_t outBytes = inflatedSize;
  const tinfl_status status = tinfl_decompress(inflator, inputBuf, &inBytes, nullptr, outputBuf, &outBytes,
                                               TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(inflator);

  if (status != TINFL_STATUS_DONE) {
    INX_SERIAL.printf("[%lu] [ZIP] tinfl_decompress() failed with status %d\n", millis(), status);
    return false;
  }

  return true;
}

bool ZipFile::loadAllFileStatSlims() {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return false;
  }

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);
  ZipServiceBudget service;

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;

    FileStatSlim fileStat = {};

    file.seekCur(6);
    file.read(&fileStat.method, 2);
    file.seekCur(8);
    file.read(&fileStat.compressedSize, 4);
    file.read(&fileStat.uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat.localHeaderOffset, 4);
    if (nameLen >= sizeof(itemName)) {
      // A malformed/very long entry name must not overrun the fixed central
      // directory scratch buffer. It remains accessible through the fallback
      // scanner, while normal EPUB paths stay in the PSRAM index.
      file.seekCur(nameLen + m + k);
      service.account(static_cast<size_t>(nameLen) + m + k + 46);
      continue;
    }
    file.read(itemName, nameLen);
    itemName[nameLen] = '\0';

    const std::string normalizedName = FsHelpers::normalisePath(itemName);
    fileStatSlimCache.push_back({EpubPsramString(normalizedName.c_str()), fileStat});

    file.seekCur(m + k);
    service.account(static_cast<size_t>(nameLen) + m + k + 46);
  }

  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;
  std::sort(fileStatSlimCache.begin(), fileStatSlimCache.end(),
            [](const IndexedFileStat& left, const IndexedFileStat& right) { return left.name < right.name; });
  fileStatSlimsLoaded = true;

  if (!wasOpen) {
    close();
  }
  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (fileStatSlimsLoaded) {
    const auto it = std::lower_bound(
        fileStatSlimCache.begin(), fileStatSlimCache.end(), filename,
        [](const IndexedFileStat& entry, const char* name) { return entry.name.compare(name) < 0; });
    if (it != fileStatSlimCache.end() && it->name == filename) {
      *fileStat = it->stat;
      return true;
    }
    return false;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return false;
  }

  uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  bool wrapped = false;
  bool found = false;

  file.seek(startPos);

  uint32_t sig;
  char itemName[256];
  ZipServiceBudget service;

  while (true) {
    uint32_t entryStart = file.position();

    if (file.read(&sig, 4) != 4 || sig != 0x02014b50) {
      if (!wrapped && lastCentralDirPosValid && startPos != zipDetails.centralDirOffset) {
        file.seek(zipDetails.centralDirOffset);
        wrapped = true;
        continue;
      }
      break;
    }

    if (wrapped && entryStart >= startPos) {
      break;
    }

    file.seekCur(6);
    file.read(&fileStat->method, 2);
    file.seekCur(8);
    file.read(&fileStat->compressedSize, 4);
    file.read(&fileStat->uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat->localHeaderOffset, 4);

    if (nameLen < 256) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';

      if (strcmp(itemName, filename) == 0) {
        file.seekCur(m + k);
        lastCentralDirPos = file.position();
        lastCentralDirPosValid = true;
        found = true;
        break;
      }
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(m + k);
    service.account(static_cast<size_t>(nameLen) + m + k + 46);
  }

  if (!wasOpen) {
    close();
  }
  return found;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return -1;
  }

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);
  if (!wasOpen) {
    close();
  }

  if (read != localHeaderSize) {
    INX_SERIAL.printf("[%lu] [ZIP] Something went wrong reading the local header\n", millis());
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) != 0x04034b50) {
    INX_SERIAL.printf("[%lu] [ZIP] Not a valid zip file header\n", millis());
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    INX_SERIAL.printf("[%lu] [ZIP] File too small to be a valid zip\n", millis());
    if (!wasOpen) {
      close();
    }
    return false;
  }

  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for EOCD scan buffer\n", millis());
    if (!wasOpen) {
      close();
    }
    return false;
  }

  file.seek(fileSize - scanRange);
  file.read(buffer, scanRange);

  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    if (*reinterpret_cast<uint32_t*>(&buffer[i]) == signature) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    INX_SERIAL.printf("[%lu] [ZIP] EOCD signature not found in zip file\n", millis());
    free(buffer);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  zipDetails.totalEntries = *reinterpret_cast<uint16_t*>(&buffer[foundOffset + 10]);
  zipDetails.centralDirOffset = *reinterpret_cast<uint32_t*>(&buffer[foundOffset + 16]);
  zipDetails.isSet = true;

  free(buffer);
  if (!wasOpen) {
    close();
  }
  return true;
}

bool ZipFile::open() {
  if (!SdMan.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::vector<SizeTarget>& targets, std::vector<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return 0;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return 0;
  }

  file.seek(zipDetails.centralDirOffset);

  int matched = 0;
  uint32_t sig;
  char itemName[256];
  ZipServiceBudget service;

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;

    file.seekCur(6);
    uint16_t method;
    file.read(&method, 2);
    file.seekCur(8);
    uint32_t compressedSize, uncompressedSize;
    file.read(&compressedSize, 4);
    file.read(&uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    uint32_t localHeaderOffset;
    file.read(&localHeaderOffset, 4);

    if (nameLen < 256) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';

      uint64_t hash = fnvHash64(itemName, nameLen);
      SizeTarget key = {hash, nameLen, 0};

      auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targets.end() && it->hash == hash && it->len == nameLen) {
        if (it->index < sizes.size()) {
          sizes[it->index] = uncompressedSize;
          matched++;
        }
        ++it;
      }
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(m + k);
    service.account(static_cast<size_t>(nameLen) + m + k + 46);
  }

  if (!wasOpen) {
    close();
  }

  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return nullptr;
  }

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) {
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for output buffer (%zu bytes)\n", millis(), dataSize);
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (fileStat.method == MZ_NO_COMPRESSION) {
    const size_t dataRead = file.read(data, inflatedDataSize);
    if (!wasOpen) {
      close();
    }

    if (dataRead != inflatedDataSize) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to read data\n", millis());
      free(data);
      return nullptr;
    }

  } else if (fileStat.method == MZ_DEFLATED) {
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for decompression buffer\n", millis());
      if (!wasOpen) {
        close();
      }
      return nullptr;
    }

    const size_t dataRead = file.read(deflatedData, deflatedDataSize);
    if (!wasOpen) {
      close();
    }

    if (dataRead != deflatedDataSize) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to read data, expected %d got %d\n", millis(), deflatedDataSize, dataRead);
      free(deflatedData);
      free(data);
      return nullptr;
    }

    const bool success = inflateOneShot(deflatedData, deflatedDataSize, data, inflatedDataSize);
    free(deflatedData);

    if (!success) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to inflate file\n", millis());
      free(data);
      return nullptr;
    }

  } else {
    INX_SERIAL.printf("[%lu] [ZIP] Unsupported compression method\n", millis());
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

namespace {
constexpr size_t kIncrementalZipInputBytes = 4096;
}

ZipFile::Stream::~Stream() { release(); }

void ZipFile::Stream::release() {
  if (inflator_) {
    free(inflator_);
    inflator_ = nullptr;
  }
  if (inputBuffer_) {
    free(inputBuffer_);
    inputBuffer_ = nullptr;
  }
  if (window_) {
    free(window_);
    window_ = nullptr;
  }
  if (openedByStream_) {
    owner_.close();
    openedByStream_ = false;
  }
}

bool ZipFile::Stream::begin(const char* filename) {
  if (!filename || !*filename) {
    return false;
  }

  openedByStream_ = !owner_.isOpen();
  if (openedByStream_ && !owner_.open()) {
    openedByStream_ = false;
    return false;
  }
  if (!owner_.loadFileStatSlim(filename, &fileStat_)) {
    release();
    return false;
  }

  const long dataOffset = owner_.getDataOffset(fileStat_);
  if (dataOffset < 0 || !owner_.file.seek(dataOffset)) {
    release();
    return false;
  }

  compressedRemaining_ = fileStat_.compressedSize;
  finished_ = fileStat_.uncompressedSize == 0;
  failed_ = false;
  if (fileStat_.method == MZ_NO_COMPRESSION) {
    inputBuffer_ = static_cast<uint8_t*>(malloc(kIncrementalZipInputBytes));
    if (!inputBuffer_) {
      release();
      return false;
    }
    return true;
  }

  if (fileStat_.method != MZ_DEFLATED) {
    INX_SERIAL.printf("[%lu] [ZIP] Unsupported incremental compression method=%u\n", millis(),
                      static_cast<unsigned>(fileStat_.method));
    release();
    return false;
  }

  inflator_ = malloc(sizeof(tinfl_decompressor));
  inputBuffer_ = static_cast<uint8_t*>(malloc(kIncrementalZipInputBytes));
  window_ = static_cast<uint8_t*>(malloc(TINFL_LZ_DICT_SIZE));
  if (!inflator_ || !inputBuffer_ || !window_) {
    INX_SERIAL.printf("[%lu] [ZIP] Incremental stream allocation failed\n", millis());
    release();
    return false;
  }
  memset(inflator_, 0, sizeof(tinfl_decompressor));
  memset(window_, 0, TINFL_LZ_DICT_SIZE);
  tinfl_init(static_cast<tinfl_decompressor*>(inflator_));
  return true;
}

std::unique_ptr<ZipFile::Stream> ZipFile::openStream(const char* filename) {
  std::unique_ptr<Stream> stream(new Stream(*this));
  if (!stream->begin(filename)) {
    return nullptr;
  }
  return stream;
}

ZipFile::Stream::Result ZipFile::Stream::pump(Print& out, const size_t maxOutputBytes) {
  if (failed_) {
    return Result::Error;
  }
  if (finished_) {
    return Result::Done;
  }
  if (maxOutputBytes == 0) {
    return Result::More;
  }

  if (fileStat_.method == MZ_NO_COMPRESSION) {
    const size_t remaining = compressedRemaining_;
    const size_t want = std::min({remaining, maxOutputBytes, kIncrementalZipInputBytes});
    const size_t read = owner_.file.read(inputBuffer_, want);
    if (read != want || out.write(inputBuffer_, read) != read) {
      failed_ = true;
      return Result::Error;
    }
    compressedRemaining_ -= read;
    finished_ = compressedRemaining_ == 0;
    return finished_ ? Result::Done : Result::More;
  }

  size_t emitted = 0;
  while (emitted < maxOutputBytes) {
    if (pendingBytes_ > 0) {
      const size_t take = std::min(pendingBytes_, maxOutputBytes - emitted);
      if (out.write(window_ + pendingCursor_, take) != take) {
        failed_ = true;
        return Result::Error;
      }
      pendingCursor_ = (pendingCursor_ + take) & (TINFL_LZ_DICT_SIZE - 1);
      pendingBytes_ -= take;
      emitted += take;
      continue;
    }

    if (finished_) {
      return Result::Done;
    }

    if (inputCursor_ >= inputFilled_) {
      if (compressedRemaining_ == 0) {
        failed_ = true;
        INX_SERIAL.printf("[%lu] [ZIP] Incremental stream unexpected EOF\n", millis());
        return Result::Error;
      }
      const size_t want = std::min(compressedRemaining_, kIncrementalZipInputBytes);
      inputFilled_ = owner_.file.read(inputBuffer_, want);
      inputCursor_ = 0;
      if (inputFilled_ != want) {
        failed_ = true;
        return Result::Error;
      }
      compressedRemaining_ -= inputFilled_;
    }

    size_t inBytes = inputFilled_ - inputCursor_;
    // tinfl's wrapping mode requires the complete output ring to have a
    // power-of-two size. Passing the caller's 12 KiB slice here made the
    // effective ring 12 KiB and immediately returned TINFL_STATUS_BAD_PARAM.
    // Inflate into the remaining contiguous part of the fixed 32 KiB ring,
    // then use pendingBytes_ above to emit no more than maxOutputBytes to the
    // caller on this reader-loop slice.
    size_t outBytes = TINFL_LZ_DICT_SIZE - windowCursor_;
    const tinfl_status status = tinfl_decompress(static_cast<tinfl_decompressor*>(inflator_), inputBuffer_ + inputCursor_,
                                                 &inBytes, window_, window_ + windowCursor_, &outBytes,
                                                 compressedRemaining_ > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
    inputCursor_ += inBytes;
    pendingCursor_ = windowCursor_;
    pendingBytes_ = outBytes;
    windowCursor_ = (windowCursor_ + outBytes) & (TINFL_LZ_DICT_SIZE - 1);

    if (status == TINFL_STATUS_DONE) {
      finished_ = true;
    } else if (status < 0 ||
               (inBytes == 0 && outBytes == 0 &&
                (status != TINFL_STATUS_NEEDS_MORE_INPUT || inputCursor_ < inputFilled_ || compressedRemaining_ == 0))) {
      failed_ = true;
      INX_SERIAL.printf("[%lu] [ZIP] Incremental inflate failed status=%d\n", millis(), static_cast<int>(status));
      return Result::Error;
    }
  }

  return (finished_ && pendingBytes_ == 0) ? Result::Done : Result::More;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t requestedChunkSize,
                               const size_t maxOutputBytes) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) {
    return false;
  }

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const size_t chunkSize = std::max(kMinimumStreamChunkSize, requestedChunkSize);
  ZipServiceBudget service;

  if (fileStat.method == MZ_NO_COMPRESSION) {
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for buffer\n", millis());
      if (!wasOpen) {
        close();
      }
      return false;
    }

    size_t remaining = inflatedDataSize;
    size_t emitted = 0;
    while (remaining > 0) {
      const size_t budgetLeft = maxOutputBytes - emitted;
      if (budgetLeft == 0) break;
      const size_t dataRead = file.read(buffer, std::min({remaining, chunkSize, budgetLeft}));
      if (dataRead == 0) {
        INX_SERIAL.printf("[%lu] [ZIP] Could not read more bytes\n", millis());
        free(buffer);
        if (!wasOpen) {
          close();
        }
        return false;
      }

      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (!wasOpen) close();
        return false;
      }
      remaining -= dataRead;
      emitted += dataRead;
      service.account(dataRead);
    }

    if (!wasOpen) {
      close();
    }
    free(buffer);
    return remaining == 0 || emitted == maxOutputBytes;
  }

  if (fileStat.method == MZ_DEFLATED) {
    const auto inflator = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
    if (!inflator) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for inflator\n", millis());
      if (!wasOpen) {
        close();
      }
      return false;
    }
    memset(inflator, 0, sizeof(tinfl_decompressor));
    tinfl_init(inflator);

    const auto fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for zip file read buffer\n", millis());
      free(inflator);
      if (!wasOpen) {
        close();
      }
      return false;
    }

    const auto outputBuffer = static_cast<uint8_t*>(malloc(TINFL_LZ_DICT_SIZE));
    if (!outputBuffer) {
      INX_SERIAL.printf("[%lu] [ZIP] Failed to allocate memory for dictionary\n", millis());
      free(inflator);
      free(fileReadBuffer);
      if (!wasOpen) {
        close();
      }
      return false;
    }
    memset(outputBuffer, 0, TINFL_LZ_DICT_SIZE);

    size_t fileRemainingBytes = deflatedDataSize;
    size_t processedOutputBytes = 0;
    size_t fileReadBufferFilledBytes = 0;
    size_t fileReadBufferCursor = 0;
    size_t outputCursor = 0;
    while (true) {
      if (fileReadBufferCursor >= fileReadBufferFilledBytes) {
        if (fileRemainingBytes == 0) {
          break;
        }

        fileReadBufferFilledBytes =
            file.read(fileReadBuffer, fileRemainingBytes < chunkSize ? fileRemainingBytes : chunkSize);
        fileRemainingBytes -= fileReadBufferFilledBytes;
        fileReadBufferCursor = 0;
        service.account(fileReadBufferFilledBytes);

        if (fileReadBufferFilledBytes == 0) {
          break;
        }
      }

      size_t inBytes = fileReadBufferFilledBytes - fileReadBufferCursor;

      size_t outBytes = TINFL_LZ_DICT_SIZE - outputCursor;

      const tinfl_status status = tinfl_decompress(inflator, fileReadBuffer + fileReadBufferCursor, &inBytes,
                                                   outputBuffer, outputBuffer + outputCursor, &outBytes,
                                                   fileRemainingBytes > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);

      fileReadBufferCursor += inBytes;

      if (outBytes > 0) {
        const size_t budgetLeft = maxOutputBytes - processedOutputBytes;
        const size_t bytesToWrite = std::min(outBytes, budgetLeft);
        if (bytesToWrite > 0 && out.write(outputBuffer + outputCursor, bytesToWrite) != bytesToWrite) {
          INX_SERIAL.printf("[%lu] [ZIP] Failed to write all output bytes to stream\n", millis());
          if (!wasOpen) {
            close();
          }
          free(outputBuffer);
          free(fileReadBuffer);
          free(inflator);
          return false;
        }

        outputCursor = (outputCursor + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        processedOutputBytes += bytesToWrite;
        service.account(outBytes);
        if (processedOutputBytes == maxOutputBytes) {
          if (!wasOpen) close();
          free(inflator);
          free(fileReadBuffer);
          free(outputBuffer);
          return true;
        }
      }

      // Deflate can perform a long run of CPU-only iterations. The time budget
      // still services it even if this iteration produced no output bytes.
      service.account(0);

      if (status < 0) {
        INX_SERIAL.printf("[%lu] [ZIP] tinfl_decompress() failed with status %d\n", millis(), status);
        if (!wasOpen) {
          close();
        }
        free(outputBuffer);
        free(fileReadBuffer);
        free(inflator);
        return false;
      }

      if (status == TINFL_STATUS_DONE) {
        EPUB_PERF_LOG("[%lu] [ZIP] Decompressed %d bytes into %d bytes\n", millis(), deflatedDataSize,
                      inflatedDataSize);
        if (!wasOpen) {
          close();
        }
        free(inflator);
        free(fileReadBuffer);
        free(outputBuffer);
        return true;
      }
    }

    INX_SERIAL.printf("[%lu] [ZIP] Unexpected EOF\n", millis());
    if (!wasOpen) {
      close();
    }
    free(outputBuffer);
    free(fileReadBuffer);
    free(inflator);
    return false;
  }

  if (!wasOpen) {
    close();
  }

  INX_SERIAL.printf("[%lu] [ZIP] Unsupported compression method\n", millis());
  return false;
}
