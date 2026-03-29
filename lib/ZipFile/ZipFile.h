#pragma once

/**
 * @file ZipFile.h
 * @brief Public interface and types for ZipFile.
 */

#include <SdFat.h>

#include <memory>
#include <string>
#include <vector>

#include "../Epub/Epub/PsramAllocator.h"

class ZipFile {
 public:
  struct FileStatSlim {
    uint16_t method;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t localHeaderOffset;
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint16_t totalEntries;
    bool isSet;
  };

  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };

  struct IndexedFileStat {
    EpubPsramString name;
    FileStatSlim stat;
  };

  /**
   * Resumable reader for one ZIP entry. Each pump emits at most the caller's
   * requested uncompressed byte budget, retaining deflate state between
   * calls. It is intentionally single-task/cooperative: the owning ZipFile's
   * handle remains reserved until this reader is destroyed.
   */
  class Stream final {
   public:
    enum class Result : uint8_t { More, Done, Error };

    ~Stream();
    Result pump(Print& out, size_t maxOutputBytes);
    bool finished() const { return finished_; }

   private:
    friend class ZipFile;
    explicit Stream(ZipFile& owner) : owner_(owner) {}
    bool begin(const char* filename);
    void release();

    ZipFile& owner_;
    FileStatSlim fileStat_ = {};
    bool openedByStream_ = false;
    bool finished_ = false;
    bool failed_ = false;
    size_t compressedRemaining_ = 0;
    size_t inputFilled_ = 0;
    size_t inputCursor_ = 0;
    size_t windowCursor_ = 0;
    size_t pendingCursor_ = 0;
    size_t pendingBytes_ = 0;
    void* inflator_ = nullptr;
    uint8_t* inputBuffer_ = nullptr;
    uint8_t* window_ = nullptr;
  };

  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  FsFile file;
  ZipDetails zipDetails = {0, 0, false};
  std::vector<IndexedFileStat, EpubPsramAllocator<IndexedFileStat>> fileStatSlimCache;
  bool fileStatSlimsLoaded = false;

  uint32_t lastCentralDirPos = 0;
  bool lastCentralDirPosValid = false;

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  ~ZipFile() = default;

  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  bool hasFileStatSlims() const { return fileStatSlimsLoaded; }
  size_t entryCount() const { return fileStatSlimCache.size(); }
  bool getInflatedFileSize(const char* filename, size_t* size);
  std::unique_ptr<Stream> openStream(const char* filename);

  int fillUncompressedSizes(std::vector<SizeTarget>& targets, std::vector<uint32_t>& sizes);

  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  /**
   * Streams an entry with bounded watchdog service. `maxOutputBytes` permits a
   * metadata probe to stop after a small uncompressed prefix instead of
   * extracting a whole image just to learn its dimensions.
   */
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize,
                        size_t maxOutputBytes = static_cast<size_t>(-1));
};
