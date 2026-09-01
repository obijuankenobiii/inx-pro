/**
 * @file Section.cpp
 * @brief Definitions for Section.
 */

#include "Section.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <exception>
#include <new>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include "Page.h"
#include "ImagePrefetch.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 81;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(float) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(bool) + sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint16_t MAX_CACHED_PAGE_OFFSETS = 2048;
}

Section::Section(const std::shared_ptr<Epub>& epubIn, const int spineIndexIn, GfxRenderer& rendererIn)
    : epub(epubIn),
      spineIndex(spineIndexIn),
      renderer(rendererIn),
      filePath(epubIn->getCachePath() + "/sections/" + std::to_string(spineIndexIn) + ".bin") {}

Section::Section(const std::string& cachePath, const int spineIndexIn, GfxRenderer& rendererIn)
    : epub(nullptr),
      spineIndex(spineIndexIn),
      renderer(rendererIn),
      filePath(cachePath + "/sections/" + std::to_string(spineIndexIn) + ".bin") {}

Section::~Section() {
  cancelIncrementalBuild();
  clearPageCache();
  if (file) {
    file.close();
  }
}

void Section::clearPageCache() {
  for (CachedPage& entry : pageCache) {
    entry.page.reset();
    entry.pageIndex = -1;
    entry.usedAt = 0;
  }
  pageCacheClock = 0;
}

/**
 * Handles completion of a page during section creation.
 * Serializes the page to the section file and increments the page count.
 *
 * @param page Unique pointer to the completed page
 * @return The file position where the page was written, or 0 on failure
 */
uint32_t Section::onPageComplete(std::unique_ptr<Page> page, const std::function<void(Page&, uint16_t)>& pageBuiltFn) {
  EpubImagePrefetch::IoLock ioLock;
  if (!file) {
    INX_SERIAL.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }
  const uint32_t position = file.position();
  if (pageBuiltFn) {
    pageBuiltFn(*page, pageCount);
  }
  if (!page->serialize(file)) {
    INX_SERIAL.printf("[%lu] [SCT] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }
  pageCount++;
  return position;
}

/**
 * Writes the header information to the section file.
 * Includes version, rendering settings, page count, and LUT offset.
 *
 * @param fontId Font identifier for text rendering
 * @param lineCompression Line spacing factor
 * @param extraParagraphSpacing Whether to add extra spacing between paragraphs
 * @param paragraphAlignment Default paragraph alignment
 * @param viewportWidth Available width for layout
 * @param viewportHeight Available height for layout
 * @param hyphenationEnabled Whether hyphenation is enabled
 */
void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const float wordSpacing,
                                     const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                     const uint16_t viewportWidth, const uint16_t viewportHeight,
                                     const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                                     const bool bionicReadingEnabled) {
  if (!file) return;
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, wordSpacing);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, respectCssParagraphIndent);
  serialization::writePod(file, bionicReadingEnabled);
  serialization::writePod(file, pageCount);
  serialization::writePod(file, static_cast<uint32_t>(0));
}

/**
 * Loads and verifies a section file from disk.
 * Checks file version and ensures all rendering settings match the current request.
 *
 * @param fontId Font identifier to verify against
 * @param lineCompression Line spacing factor to verify against
 * @param extraParagraphSpacing Paragraph spacing setting to verify against
 * @param paragraphAlignment Paragraph alignment to verify against
 * @param viewportWidth Viewport width to verify against
 * @param viewportHeight Viewport height to verify against
 * @param hyphenationEnabled Hyphenation setting to verify against
 * @return true if section file exists and settings match, false otherwise
 */
bool Section::loadSectionFile(const int fontId, const float lineCompression, const float wordSpacing,
                              const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                              const uint16_t viewportWidth, const uint16_t viewportHeight,
                              const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                              const bool bionicReadingEnabled) {
  if (file) {
    file.close();
  }
  lutOffset = 0;
  pageOffsets.clear();
  clearPageCache();

  if (!SdMan.openFileForRead("SCT", filePath, file)) return false;

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    clearCache();
    return false;
  }

  int storedFontId;
  float storedLineCompression;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing;
  uint8_t storedParagraphAlignment;
  uint16_t storedViewportWidth;
  uint16_t storedViewportHeight;
  bool storedHyphenationEnabled;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount;
  uint32_t storedLutOffset;

  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedWordSpacing);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  serialization::readPod(file, storedRespectCssIndent);
  serialization::readPod(file, storedBionicReadingEnabled);
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);

  bool settingsMatch = true;
  settingsMatch &= (storedFontId == fontId);
  settingsMatch &= (abs(storedLineCompression - lineCompression) < 0.001f);
  settingsMatch &= (abs(storedWordSpacing - wordSpacing) < 0.001f);
  settingsMatch &= (storedExtraParagraphSpacing == extraParagraphSpacing);
  settingsMatch &= (storedParagraphAlignment == paragraphAlignment);
  settingsMatch &= (storedViewportWidth == viewportWidth);
  settingsMatch &= (storedViewportHeight == viewportHeight);
  settingsMatch &= (storedHyphenationEnabled == hyphenationEnabled);
  settingsMatch &= (storedRespectCssIndent == respectCssParagraphIndent);
  settingsMatch &= (storedBionicReadingEnabled == bionicReadingEnabled);

  if (!settingsMatch) {
    file.close();
    clearCache();
    return false;
  }

  pageCount = storedPageCount;
  lutOffset = storedLutOffset;

  if (pageCount == 0 || lutOffset == 0) {
    INX_SERIAL.printf("[%lu] [SCT] loadSectionFile: invalid empty section cache spine=%d pages=%u lut=%lu file=%s\n",
                  millis(), spineIndex, static_cast<unsigned>(pageCount), static_cast<unsigned long>(lutOffset),
                  filePath.c_str());
    file.close();
    clearCache();
    return false;
  }

  if (pageCount > 0 && pageCount <= MAX_CACHED_PAGE_OFFSETS && lutOffset > 0) {
    try {
      pageOffsets.resize(pageCount);
      file.seek(lutOffset);
      for (uint16_t i = 0; i < pageCount; ++i) {
        serialization::readPod(file, pageOffsets[i]);
      }
    } catch (...) {
      pageOffsets.clear();
    }
  }

  return true;
}

bool Section::loadSectionFileForPreview(int* outFontId) {
  if (file) {
    file.close();
  }
  lutOffset = 0;
  pageOffsets.clear();
  pageCount = 0;
  clearPageCache();

  if (!SdMan.openFileForRead("SCT", filePath, file)) return false;

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    return false;
  }

  int storedFontId = 0;
  float storedLineCompression = 1.0f;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing = false;
  uint8_t storedParagraphAlignment = 0;
  uint16_t storedViewportWidth = 0;
  uint16_t storedViewportHeight = 0;
  bool storedHyphenationEnabled = false;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount = 0;
  uint32_t storedLutOffset = 0;

  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedWordSpacing);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  serialization::readPod(file, storedRespectCssIndent);
  serialization::readPod(file, storedBionicReadingEnabled);
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);

  pageCount = storedPageCount;
  lutOffset = storedLutOffset;
  if (outFontId != nullptr) {
    *outFontId = storedFontId;
  }
  return pageCount > 0 && lutOffset != 0;
}

std::unique_ptr<Page> Section::loadCachedPage(const std::string& cachePath, const int spineIndex,
                                              const int pageNumber) {
  if (spineIndex < 0 || pageNumber < 0) {
    return nullptr;
  }

  FsFile sectionFile;
  const std::string path = cachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
  if (!SdMan.openFileForRead("SCT", path, sectionFile)) {
    return nullptr;
  }

  uint8_t version = 0;
  serialization::readPod(sectionFile, version);
  if (version != SECTION_FILE_VERSION) {
    sectionFile.close();
    return nullptr;
  }

  int storedFontId = 0;
  float storedLineCompression = 1.0f;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing = false;
  uint8_t storedParagraphAlignment = 0;
  uint16_t storedViewportWidth = 0;
  uint16_t storedViewportHeight = 0;
  bool storedHyphenationEnabled = false;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount = 0;
  uint32_t storedLutOffset = 0;

  serialization::readPod(sectionFile, storedFontId);
  serialization::readPod(sectionFile, storedLineCompression);
  serialization::readPod(sectionFile, storedWordSpacing);
  serialization::readPod(sectionFile, storedExtraParagraphSpacing);
  serialization::readPod(sectionFile, storedParagraphAlignment);
  serialization::readPod(sectionFile, storedViewportWidth);
  serialization::readPod(sectionFile, storedViewportHeight);
  serialization::readPod(sectionFile, storedHyphenationEnabled);
  serialization::readPod(sectionFile, storedRespectCssIndent);
  serialization::readPod(sectionFile, storedBionicReadingEnabled);
  serialization::readPod(sectionFile, storedPageCount);
  serialization::readPod(sectionFile, storedLutOffset);

  if (storedPageCount == 0 || storedLutOffset == 0 || pageNumber >= static_cast<int>(storedPageCount)) {
    sectionFile.close();
    return nullptr;
  }

  uint32_t pagePos = 0;
  sectionFile.seek(storedLutOffset + sizeof(uint32_t) * pageNumber);
  serialization::readPod(sectionFile, pagePos);
  if (pagePos == 0) {
    sectionFile.close();
    return nullptr;
  }

  sectionFile.seek(pagePos);
  auto page = Page::deserialize(sectionFile);
  sectionFile.close();
  return page;
}

/**
 * Creates a new section file by parsing the HTML content and building pages.
 * Streams chapter HTML directly from the EPUB into Expat, so pagination can
 * begin before the entire chapter has been materialized on the SD card.
 * Images are converted during layout unless skipImages is true.
 *
 * @param fontId Font identifier for text rendering
 * @param headerFontId Font identifier for header rendering
 * @param lineCompression Line spacing factor
 * @param extraParagraphSpacing Whether to add extra spacing between paragraphs
 * @param paragraphAlignment Default paragraph alignment
 * @param viewportWidth Available width for layout
 * @param viewportHeight Available height for layout
 * @param hyphenationEnabled Whether hyphenation is enabled
 * @param popupFn Optional callback for progress popups during image conversion
 * @param skipImages If true, skip processing new images and only use existing cached images
 * @return true if section file was successfully created, false otherwise
 */
bool Section::createSectionFile(const int fontId, const int headerFontId, const int maxFontId,
                                const float lineCompression, const float wordSpacing, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled,
                                const bool respectCssParagraphIndent, const bool bionicReadingEnabled,
                                const std::function<void()>& popupFn, bool skipImages,
                                const std::function<void(Page&, uint16_t)>& pageBuiltFn,
                                const bool warmImageDisplayCache, const ImageRenderMode warmImageRenderMode,
                                const bool warmImageQuality, const int warmImageYOffset) {
  clearPageCache();
  pageOffsets.clear();
  pageCount = 0;
  const auto localPath = epub->getSpineItem(spineIndex).href;

  std::string contentBasePath = "";
  size_t lastSlash = localPath.find_last_of('/');
  if (lastSlash != std::string::npos) {
    contentBasePath = localPath.substr(0, lastSlash);
  }

  SdMan.mkdir((epub->getCachePath() + "/sections").c_str());

  std::vector<uint32_t> lut;

  ChapterHtmlSlimParser visitor(
      localPath, *epub, epub->getCachePath(), contentBasePath, renderer, fontId, headerFontId, maxFontId,
      lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled,
      [this, &lut, &pageBuiltFn](std::unique_ptr<Page> page) {
        lut.emplace_back(this->onPageComplete(std::move(page), pageBuiltFn));
      },
      warmImageDisplayCache, warmImageRenderMode, warmImageQuality, warmImageYOffset, popupFn);

  visitor.internalPath = localPath;

  Hyphenator::setPreferredLanguage(epub->getLanguage());

  if (!SdMan.openFileForWrite("SCT", filePath, file)) return false;

  writeSectionFileHeader(fontId, lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled);

  bool success = false;
  try {
    success = visitor.parseAndBuildPages(skipImages);
  } catch (const std::bad_alloc& e) {
#if defined(ARDUINO_ARCH_ESP32)
    INX_SERIAL.printf("[%lu] [SCT] createSectionFile: OOM while parsing spine=%d href=%s (%s) heap=%u psram=%u largest=%u\n",
                  millis(), spineIndex, localPath.c_str(), e.what(), static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#else
    INX_SERIAL.printf("[%lu] [SCT] createSectionFile: OOM while parsing spine=%d href=%s (%s)\n", millis(), spineIndex,
                  localPath.c_str(), e.what());
#endif
    success = false;
  } catch (const std::exception& e) {
    INX_SERIAL.printf("[%lu] [SCT] createSectionFile: exception while parsing spine=%d href=%s (%s)\n", millis(),
                  spineIndex, localPath.c_str(), e.what());
    success = false;
  } catch (...) {
    INX_SERIAL.printf("[%lu] [SCT] createSectionFile: unknown exception while parsing spine=%d href=%s\n", millis(),
                  spineIndex, localPath.c_str());
    success = false;
  }

  if (!success) {
    INX_SERIAL.printf(
        "[%lu] [SCT] createSectionFile: parser returned false spine=%d href=%s file=%s book=%s title=%s pages=%u "
        "\n",
        millis(), spineIndex, localPath.c_str(), filePath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str(),
        static_cast<unsigned>(pageCount));
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  if (pageCount == 0) {
    INX_SERIAL.printf("[%lu] [SCT] createSectionFile: zero-page section spine=%d href=%s book=%s title=%s\n", millis(),
                  spineIndex, localPath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      INX_SERIAL.printf("[%lu] [SCT] createSectionFile: invalid LUT entry (page offset 0) spine=%d — discarding section\n",
                    millis(), spineIndex);
      file.close();
      SdMan.remove(filePath.c_str());
      return false;
    }
  }

  {
    EpubImagePrefetch::IoLock ioLock;
    const uint32_t lutOffset = file.position();
    for (const uint32_t& pos : lut) {
      serialization::writePod(file, pos);
    }

    file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
    serialization::writePod(file, pageCount);
    serialization::writePod(file, lutOffset);
    file.close();
  }
  epub->flushImageMetadata();
  return true;
}

bool Section::beginIncrementalBuild(
    const int fontId, const int headerFontId, const int maxFontId, const float lineCompression,
    const float wordSpacing, const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
    const uint16_t viewportWidth, const uint16_t viewportHeight, const bool hyphenationEnabled,
    const bool respectCssParagraphIndent, const bool bionicReadingEnabled, const bool skipImages) {
  if (!epub || incrementalBuildActive()) {
    return false;
  }

  cancelIncrementalBuild();
  clearPageCache();
  pageOffsets.clear();
  pageCount = 0;
  incrementalLut_.clear();
  incrementalBytesParsed_ = 0;
  incrementalTotalBytes_ = 0;
  incrementalBuildStartedAt_ = millis();
  incrementalLastProgressLogAt_ = incrementalBuildStartedAt_;

  const std::string localPath = epub->getSpineItem(spineIndex).href;
  if (localPath.empty()) {
    incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
    return false;
  }
  (void)epub->getItemSize(localPath, &incrementalTotalBytes_);
  const size_t lastSlash = localPath.find_last_of('/');
  const std::string contentBasePath =
      lastSlash == std::string::npos ? std::string() : localPath.substr(0, lastSlash);

  SdMan.mkdir((epub->getCachePath() + "/sections").c_str());
  incrementalTempPath_ = filePath + ".build.tmp";
  SdMan.remove(incrementalTempPath_.c_str());
  if (!SdMan.openFileForWrite("SCT", incrementalTempPath_, file)) {
    incrementalTempPath_.clear();
    incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
    return false;
  }

  writeSectionFileHeader(fontId, lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled);

  incrementalParser_ = std::make_unique<ChapterHtmlSlimParser>(
      localPath, *epub, epub->getCachePath(), contentBasePath, renderer, fontId, headerFontId, maxFontId,
      lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled,
      [this](std::unique_ptr<Page> page) { incrementalLut_.emplace_back(onPageComplete(std::move(page), nullptr)); });
  incrementalParser_->internalPath = localPath;

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  if (!incrementalParser_->beginIncremental(skipImages)) {
    cancelIncrementalBuild();
    incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
    return false;
  }

  incrementalStream_ = epub->openItemStream(localPath);
  if (!incrementalStream_) {
    cancelIncrementalBuild();
    incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
    return false;
  }

  incrementalBuildStatus_ = IncrementalBuildStatus::Building;
  INX_SERIAL.printf("[%lu] [SCT-IDLE] started spine=%d href=%s\n", millis(), spineIndex, localPath.c_str());
  return true;
}

Section::IncrementalBuildStatus Section::stepIncrementalBuild(const size_t maxInflatedBytes) {
  if (incrementalBuildStatus_ != IncrementalBuildStatus::Building || !incrementalParser_ || !incrementalStream_) {
    return incrementalBuildStatus_;
  }

  class ParserSink final : public Print {
   public:
    explicit ParserSink(ChapterHtmlSlimParser& parser) : parser_(parser) {}
    size_t write(const uint8_t* data, const size_t size) override {
      if (!parser_.feedIncremental(data, size)) {
        return 0;
      }
      bytesWritten_ += size;
      return size;
    }
    size_t write(const uint8_t value) override { return write(&value, 1); }
    size_t bytesWritten() const { return bytesWritten_; }

   private:
    ChapterHtmlSlimParser& parser_;
    size_t bytesWritten_ = 0;
  } sink(*incrementalParser_);

  Epub::ItemStream::Result streamResult = Epub::ItemStream::Result::Error;
  try {
    streamResult = incrementalStream_->pump(sink, maxInflatedBytes);
  } catch (const std::bad_alloc&) {
    INX_SERIAL.printf("[%lu] [SCT-IDLE] OOM spine=%d\n", millis(), spineIndex);
    streamResult = Epub::ItemStream::Result::Error;
  } catch (...) {
    INX_SERIAL.printf("[%lu] [SCT-IDLE] parser exception spine=%d\n", millis(), spineIndex);
    streamResult = Epub::ItemStream::Result::Error;
  }

  incrementalBytesParsed_ += sink.bytesWritten();
  const uint32_t now = millis();
  if (streamResult == Epub::ItemStream::Result::More && now - incrementalLastProgressLogAt_ >= 2000) {
    const unsigned progress =
        incrementalTotalBytes_ == 0
            ? 0
            : static_cast<unsigned>(std::min<uint64_t>(
                  99, (static_cast<uint64_t>(incrementalBytesParsed_) * 100) / incrementalTotalBytes_));
    INX_SERIAL.printf("[%lu] [SCT-IDLE] progress spine=%d %lu/%lu (%u%%) pages=%u\n", now, spineIndex,
                      static_cast<unsigned long>(incrementalBytesParsed_),
                      static_cast<unsigned long>(incrementalTotalBytes_), progress, static_cast<unsigned>(pageCount));
    incrementalLastProgressLogAt_ = now;
  }

  if (streamResult == Epub::ItemStream::Result::More) {
    return incrementalBuildStatus_;
  }
  if (streamResult == Epub::ItemStream::Result::Error || !incrementalParser_->finishIncremental()) {
    INX_SERIAL.printf("[%lu] [SCT-IDLE] failed spine=%d pages=%u\n", millis(), spineIndex,
                      static_cast<unsigned>(pageCount));
    cancelIncrementalBuild();
    incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
    return incrementalBuildStatus_;
  }

  bool valid = pageCount > 0;
  for (const uint32_t offset : incrementalLut_) {
    valid = valid && offset != 0;
  }
  if (valid) {
    EpubImagePrefetch::IoLock ioLock;
    const uint32_t buildLutOffset = file.position();
    for (const uint32_t offset : incrementalLut_) {
      serialization::writePod(file, offset);
    }
    file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
    serialization::writePod(file, pageCount);
    serialization::writePod(file, buildLutOffset);
    file.sync();
    file.close();

    const std::string backupPath = filePath + ".build.old";
    SdMan.remove(backupPath.c_str());
    const bool hadPrevious = SdMan.exists(filePath.c_str());
    const bool backedUp = !hadPrevious || SdMan.rename(filePath.c_str(), backupPath.c_str());
    const bool published = backedUp && SdMan.rename(incrementalTempPath_.c_str(), filePath.c_str());
    if (!published && backedUp && hadPrevious) {
      SdMan.rename(backupPath.c_str(), filePath.c_str());
    }
    if (published) {
      SdMan.remove(backupPath.c_str());
      incrementalTempPath_.clear();
      incrementalParser_.reset();
      incrementalStream_.reset();
      incrementalLut_.clear();
      epub->flushImageMetadata();
      incrementalBuildStatus_ = IncrementalBuildStatus::Ready;
      INX_SERIAL.printf("[%lu] [SCT-IDLE] ready spine=%d pages=%u bytes=%lu elapsed=%lums\n", millis(), spineIndex,
                        static_cast<unsigned>(pageCount), static_cast<unsigned long>(incrementalBytesParsed_),
                        static_cast<unsigned long>(millis() - incrementalBuildStartedAt_));
      return incrementalBuildStatus_;
    }
  }

  INX_SERIAL.printf("[%lu] [SCT-IDLE] publish failed spine=%d\n", millis(), spineIndex);
  cancelIncrementalBuild();
  incrementalBuildStatus_ = IncrementalBuildStatus::Failed;
  return incrementalBuildStatus_;
}

void Section::cancelIncrementalBuild() {
  incrementalParser_.reset();
  incrementalStream_.reset();
  incrementalLut_.clear();
  if (file) {
    file.close();
  }
  if (!incrementalTempPath_.empty()) {
    SdMan.remove(incrementalTempPath_.c_str());
    incrementalTempPath_.clear();
  }
  if (incrementalBuildStatus_ == IncrementalBuildStatus::Building) {
    incrementalBuildStatus_ = IncrementalBuildStatus::Idle;
  }
}

/**
 * Loads a specific page from the section file.
 * Uses the look-up table to locate and deserialize the requested page.
 *
 * @return Section-owned page, or nullptr on failure
 */
Page* Section::loadPage(const int pageIndex) {
  if (pageIndex < 0 || pageIndex >= pageCount) {
    return nullptr;
  }

  for (CachedPage& entry : pageCache) {
    if (entry.page && entry.pageIndex == pageIndex) {
      entry.usedAt = ++pageCacheClock;
      return entry.page.get();
    }
  }

  if (!file && !SdMan.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  uint32_t pagePos = 0;
  if (pageIndex < static_cast<int>(pageOffsets.size())) {
    pagePos = pageOffsets[pageIndex];
  } else {
    if (lutOffset == 0) {
      file.seek(HEADER_SIZE - sizeof(uint32_t));
      serialization::readPod(file, lutOffset);
    }
    file.seek(lutOffset + sizeof(uint32_t) * pageIndex);
    serialization::readPod(file, pagePos);
  }

  if (pagePos == 0) {
    return nullptr;
  }

  file.seek(pagePos);
  auto page = Page::deserialize(file);
  if (!page) {
    return nullptr;
  }
  if (epub) {
    page->bindEpub(epub);
  }

  CachedPage* victim = &pageCache.front();
  for (CachedPage& entry : pageCache) {
    if (!entry.page) {
      victim = &entry;
      break;
    }
    if (entry.usedAt < victim->usedAt) {
      victim = &entry;
    }
  }
  victim->page = std::move(page);
  victim->pageIndex = pageIndex;
  victim->usedAt = ++pageCacheClock;

  return victim->page.get();
}

Page* Section::loadPageFromSectionFile() { return loadPage(currentPage); }

bool Section::preloadPage(const int pageIndex) { return loadPage(pageIndex) != nullptr; }

/**
 * Removes the section file from the filesystem.
 *
 * @return true if file was successfully removed or didn't exist, false on error
 */
bool Section::clearCache() const {
  if (SdMan.exists(filePath.c_str())) {
    return SdMan.remove(filePath.c_str());
  }
  return true;
}
