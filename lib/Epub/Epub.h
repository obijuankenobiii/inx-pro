#pragma once

/**
 * @file Epub.h
 * @brief Public interface and types for Epub.
 */

#include <Print.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/PsramAllocator.h"
#include "Epub/parsers/CssParser.h"

class ZipFile;

class Epub {
 private:
  std::string tocNcxItem;
  std::string tocNavItem;
  std::string filepath;
  std::string contentBasePath;
  std::string cachePath;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  mutable std::unique_ptr<ZipFile> zipFile_;
  mutable bool zipIndexAttempted_ = false;
  // SAX image callbacks may read another ZIP entry while chapter XHTML is
  // being inflated. Keep that nested seek on a separate indexed file handle.
  mutable std::unique_ptr<ZipFile> nestedZipFile_;
  mutable bool nestedZipIndexAttempted_ = false;
  mutable uint8_t zipStreamDepth_ = 0;
  mutable std::unique_ptr<CssParser> parsedCssParser_;
  mutable bool parsedCssLoaded_ = false;

  struct ImageMetadata {
    EpubPsramString path;
    int width = 0;
    int height = 0;
    uint8_t format = 0;
    bool valid = false;
  };
  mutable std::vector<ImageMetadata, EpubPsramAllocator<ImageMetadata>> imageMetadata_;
  mutable bool imageMetadataDirty_ = false;
  // A slow-path JPEG cover is used twice in one open: first for the cover
  // screen, then to generate thumb.jpg. Keep a bounded raw copy in PSRAM so
  // the thumbnail decoder does not reopen and read it from SD again.
  mutable std::vector<uint8_t, EpubPsramAllocator<uint8_t>> coverJpegPsram_;

  ZipFile& zip() const;
  ZipFile& nestedZip() const;
  ZipFile& zipForNestedOperation() const;

  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  std::string parsedCssCachePath() const;
  bool loadParsedCssCache() const;
  bool saveParsedCssCache() const;
  void loadPersistedImageMetadata();
  bool extractCoverJpegToPathAndPsram(const std::string& itemHref, const std::string& outPath) const;
  void clearCoverJpegPsram() const;

 public:
  /**
   * Cooperative stream for one EPUB entry. Keep it alive across reader-loop
   * slices; each pump emits only the requested amount of inflated data and
   * keeps nested image reads on Epub's separate ZIP handle.
   */
  class ItemStream final {
   public:
    enum class Result : uint8_t { More, Done, Error };

    ~ItemStream();
    Result pump(Print& out, size_t maxOutputBytes);
    bool finished() const;

   private:
    struct Impl;
    explicit ItemStream(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class Epub;
  };

  explicit Epub(std::string filepath, const std::string& oldCacheDir = "");

  ~Epub();

  /** Loads book.bin from cache; on success returns immediately without re-parsing OPF/TOC/CSS. */
  bool load(bool buildIfMissing = true);
  /** Fast metadata-cache probe. Does not parse the EPUB or build missing cache files. */
  bool hasMetadataCache() const;
  bool isLoaded() const;
  bool clearCache();
  void setupCacheDir() const;

  std::string getCacheImgPath(const std::string& internalHref) const;
  bool extractItemToPath(const std::string& itemHref, const std::string& outPath, size_t chunkSize = 2048) const;
  bool extractAndConvertImageFullScreen(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                        int targetH, bool cropToFill) const;
  bool extractAndConvertImage(const std::string& itemHref, const std::string& outBmpPath, int targetW = 0,
                              int targetH = 0) const;

  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string& getBasePath() { return contentBasePath; }

  std::string getCoverBmpPath(bool cropped = false) const;
  std::string getCoverJpegPath(bool cropped = false) const;
  std::string getCoverItemHref() const;
  bool extractCoverItemToPath(const std::string& outPath) const;
  bool generateCoverBmp(bool cropped = false) const;
  std::string getThumbBmpPath() const;
  std::string getThumbJpegPath() const;
  std::string getThumbPngPath() const;
  std::string getSmallThumbBmpPath() const;
  /**
   * @param skipCoverFallback When true, skip the "extract the cover image fresh and resize it" fallback -
   * the caller already knows cover extraction just failed for this book, so retrying the same zip entry
   * here would only waste time re-failing. The packaged META-INF/thumbnail.jpg path is independent of the
   * cover and is still tried either way.
   */
  bool generateThumbBmp(bool skipCoverFallback = false) const;

  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                size_t maxOutputBytes = static_cast<size_t>(-1)) const;
  std::unique_ptr<ItemStream> openItemStream(const std::string& itemHref) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;

  /**
   * Reads only enough of an image entry to obtain intrinsic dimensions. This
   * keeps pagination independent from extraction/decoding of the full image.
   */
  bool probeImageDimensions(const std::string& itemHref, int* width, int* height) const;

  /** Returns cached dimensions for a generated image, if known for this book session. */
  bool getImageMetadata(const std::string& path, int* width, int* height, uint8_t format) const;
  void setImageMetadata(const std::string& path, int width, int height, uint8_t format, bool valid) const;
  void invalidateImageMetadata(const std::string& path) const;
  /** Atomically stores session-discovered image dimensions after a chapter is built. */
  bool flushImageMetadata();

  int getSpineItemsCount() const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  int getTocItemsCount() const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  int getSpineIndexForTextReference() const;
  /** First spine suitable for reading when opening a book (guide text ref, else TOC, else first HTML spine). */
  int getSpineIndexForInitialOpen() const;

  int getCssItemsCount() const;
  BookMetadataCache::CssEntry getCssItem(int cssIndex) const;
  std::string getCssContent(const std::string& cssPath) const;
  std::vector<std::string> getAllCssPaths() const;
  std::string getCombinedCss() const;
  /** Parsed book-level CSS dictionary shared by every chapter parser. Null when heap is too low. */
  const CssParser* getParsedCssParser(const CssParser::UsageFilter* usageFilter = nullptr) const;
  /** Explicitly drops the parsed CSS dictionary; normally it stays alive until the book is closed. */
  void releaseParsedCssParser() const;

  size_t getCumulativeSpineItemSize(int spineIndex) const;
  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
};
