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
#include "../../../src/system/EpubPerf.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// Heading font IDs are stored per h1-h6 semantic size for outline fonts, and heading/drop-cap layout changed.
// Image bounds now match the aspect-fitted raster dimensions, which also changes cached page layout.
// The generic shrink-to-fit box geometry changed; cached pages must be rebuilt.
constexpr uint8_t SECTION_FILE_VERSION = 106;
// Bionic-reader normalization also changes cached word offsets; this version
// invalidates caches produced before either geometry or text-layout change.
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(float) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(bool) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint16_t MAX_CACHED_PAGE_OFFSETS = 2048;

class ProgressiveParseSink final : public Print {
 public:
  explicit ProgressiveParseSink(ChapterHtmlSlimParser& parser) : parser_(parser) {}

  size_t write(const uint8_t* data, const size_t size) override {
    if (!data || size == 0 || !parser_.feedParse(data, size)) {
      ok_ = false;
      return 0;
    }
    bytes_ += size;
    return size;
  }

  size_t write(const uint8_t value) override { return write(&value, 1); }
  bool ok() const { return ok_; }
  size_t bytes() const { return bytes_; }

 private:
  ChapterHtmlSlimParser& parser_;
  bool ok_ = true;
  size_t bytes_ = 0;
};
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
  if (progressiveParser_) progressiveParser_->cancelParse();
  progressiveParser_.reset();
  progressiveInput_.reset();
  const bool removeIncompleteProgressive = progressiveActive_;
  const std::string incompleteProgressivePath = progressiveFilePath_;
  progressiveActive_ = false;
  progressiveFilePath_.clear();
  clearPageCache();
  if (file) {
    file.close();
  }
  if (removeIncompleteProgressive && !incompleteProgressivePath.empty()) {
    EpubImagePrefetch::IoLock ioLock;
    SdMan.remove(incompleteProgressivePath.c_str());
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
  if (!page) {
    progressiveWriteFailed_ = progressiveActive_;
    return 0;
  }
  if (!file) {
    INX_SERIAL.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    progressiveWriteFailed_ = progressiveActive_;
    return 0;
  }
  const uint32_t position = file.position();
  if (pageBuiltFn) {
    pageBuiltFn(*page, pageCount);
  }
  if (!page->serialize(file)) {
    INX_SERIAL.printf("[%lu] [SCT] Failed to serialize page %d\n", millis(), pageCount);
    progressiveWriteFailed_ = progressiveActive_;
    return 0;
  }
  if (progressiveActive_) {
    progressivePageOffsets_.push_back(position);
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
  serialization::writePod(file, Hyphenator::cacheSignature());
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

  if (epub) Hyphenator::setPreferredLanguage(epub->getLanguage());

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
  uint32_t storedHyphenationSignature = 0;
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
  serialization::readPod(file, storedHyphenationSignature);
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
  settingsMatch &= (storedHyphenationSignature == Hyphenator::cacheSignature());

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
  uint32_t storedHyphenationSignature = 0;
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
  serialization::readPod(file, storedHyphenationSignature);
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
  uint32_t storedHyphenationSignature = 0;
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
  serialization::readPod(sectionFile, storedHyphenationSignature);
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

bool Section::beginProgressiveSectionFile(const int fontId, const int headerFontId, const int maxFontId,
                                           const float lineCompression, const float wordSpacing,
                                           const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                           const uint16_t viewportWidth, const uint16_t viewportHeight,
                                           const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                                           const bool bionicReadingEnabled, const bool skipImages) {
  if (!epub || progressiveActive_) return false;

  const std::string localPath = epub->getSpineItem(spineIndex).href;
  if (localPath.empty()) return false;

  EpubImagePrefetch::IoLock ioLock;
  if (file) file.close();
  SdMan.mkdir((epub->getCachePath() + "/sections").c_str());
  progressiveFilePath_ = filePath + ".part";
  if (SdMan.exists(progressiveFilePath_.c_str()) && !SdMan.remove(progressiveFilePath_.c_str())) {
    INX_SERIAL.printf("[%lu] [SCT] Failed to remove stale progressive file: %s\n", millis(),
                      progressiveFilePath_.c_str());
    progressiveFilePath_.clear();
    return false;
  }
  if (!SdMan.openFileForWrite("SCT", progressiveFilePath_, file)) {
    progressiveFilePath_.clear();
    return false;
  }

  clearPageCache();
  pageOffsets.clear();
  progressivePageOffsets_.clear();
  pageCount = 0;
  progressiveBytesRead_ = 0;
  progressiveWriteFailed_ = false;
  writeSectionFileHeader(fontId, lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled);

  std::string contentBasePath;
  const size_t lastSlash = localPath.find_last_of('/');
  if (lastSlash != std::string::npos) contentBasePath = localPath.substr(0, lastSlash);

  progressiveParser_ = std::unique_ptr<ChapterHtmlSlimParser>(new ChapterHtmlSlimParser(
      localPath, *epub, epub->getCachePath(), contentBasePath, renderer, fontId, headerFontId, maxFontId,
      lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled,
      [this](std::unique_ptr<Page> page) { onPageComplete(std::move(page), nullptr); }, false,
      ImageRenderMode::OneBit, false, 0, nullptr));
  progressiveParser_->internalPath = localPath;
  Hyphenator::setPreferredLanguage(epub->getLanguage());
  if (!progressiveParser_->beginParse(skipImages)) {
    progressiveParser_.reset();
    file.close();
    SdMan.remove(progressiveFilePath_.c_str());
    progressiveFilePath_.clear();
    return false;
  }

  progressiveInput_ = epub->openItemStream(localPath);
  if (!progressiveInput_) {
    progressiveParser_->cancelParse();
    progressiveParser_.reset();
    file.close();
    SdMan.remove(progressiveFilePath_.c_str());
    progressiveFilePath_.clear();
    return false;
  }

  progressiveActive_ = true;
  return true;
}

bool Section::pumpProgressive(const uint32_t budgetMs) {
  if (!progressiveActive_ || !progressiveParser_ || !progressiveInput_) return false;
  // Match the normal EPUB parser's proven 16 KB Expat feed size. The smaller
  // 512-byte slices can leave some XHTML chapters in an invalid final parser
  // state even though the synchronous parser accepts the same content.
  constexpr size_t kChunkBytes = 16 * 1024;

  const auto fail = [this]() {
    if (progressiveParser_) progressiveParser_->cancelParse();
    progressiveParser_.reset();
    progressiveInput_.reset();
    progressiveActive_ = false;
    EpubImagePrefetch::IoLock ioLock;
    if (file) file.close();
    if (!progressiveFilePath_.empty()) SdMan.remove(progressiveFilePath_.c_str());
    progressiveFilePath_.clear();
    progressivePageOffsets_.clear();
    pageCount = 0;
    lutOffset = 0;
    return false;
  };

  // Exactly one small inflater/parser step per reader-loop invocation. Do not
  // wait for page 0 here: a long first page must never delay a button/touch
  // event on the active chapter.
  if (budgetMs == 0) return pageCount > 0;
  // A foreground page read owns the SD/SPI lock. The background task must
  // yield instead of waiting behind it, otherwise prefetch can add latency to
  // touch/button handling.
  if (!EpubImagePrefetch::tryLockIo()) return pageCount > 0;
  ProgressiveParseSink sink(*progressiveParser_);
  const Epub::ItemStream::Result result = progressiveInput_->pump(sink, kChunkBytes);
  EpubImagePrefetch::unlockIo();
  progressiveBytesRead_ += sink.bytes();
  if (!sink.ok() || progressiveWriteFailed_) {
    INX_SERIAL.printf("[%lu] [SCT] next chapter prefetch parser failed spine=%d pages=%u\n", millis(), spineIndex,
                      static_cast<unsigned>(pageCount));
    return fail();
  }
  if (result == Epub::ItemStream::Result::More) return pageCount > 0;
  if (result != Epub::ItemStream::Result::Done || !progressiveParser_->finishParse() || pageCount == 0) {
    size_t expectedBytes = 0;
    const std::string href = epub ? epub->getSpineItem(spineIndex).href : std::string();
    if (epub && !href.empty()) epub->getItemSize(href, &expectedBytes);
    INX_SERIAL.printf("[%lu] [SCT] next chapter prefetch stopped spine=%d result=%u pages=%u\n", millis(), spineIndex,
                      static_cast<unsigned>(result), static_cast<unsigned>(pageCount));
    INX_SERIAL.printf("[%lu] [SCT] next chapter stream bytes=%lu expected=%lu\n", millis(),
                      static_cast<unsigned long>(progressiveBytesRead_), static_cast<unsigned long>(expectedBytes));
    return fail();
  }

  EpubImagePrefetch::IoLock ioLock;
  lutOffset = file.position();
  for (const uint32_t position : progressivePageOffsets_) serialization::writePod(file, position);
  file.sync();
  file.close();

  // Patch the header through a fresh read/write handle, then publish the
  // completed file in one replace operation. The Murphy SD/FAT path can leave
  // an in-place seek-back update unwritten even though all page data was
  // flushed, which previously produced pages=0/lut=0 after a "successful"
  // prefetch.
  FsFile finalized = SdMan.open(progressiveFilePath_.c_str(), O_RDWR);
  const bool headerPatched = finalized && finalized.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount)) &&
                             finalized.write(reinterpret_cast<const uint8_t*>(&pageCount), sizeof(pageCount)) ==
                                 sizeof(pageCount) &&
                             finalized.write(reinterpret_cast<const uint8_t*>(&lutOffset), sizeof(lutOffset)) ==
                                 sizeof(lutOffset);
  if (!headerPatched) {
    INX_SERIAL.printf("[%lu] [SCT] next chapter prefetch header finalize failed spine=%d pages=%u\n", millis(),
                      spineIndex, static_cast<unsigned>(pageCount));
    if (finalized) finalized.close();
    return fail();
  }
  finalized.sync();
  finalized.close();
  // Both the device SD manager and the simulator expose remove/rename. The
  // old final file has already been validated or discarded by the caller, so
  // this two-step publish is safe here and keeps the code portable.
  const bool removedOld = !SdMan.exists(filePath.c_str()) || SdMan.remove(filePath.c_str());
  if (!removedOld || !SdMan.rename(progressiveFilePath_.c_str(), filePath.c_str())) {
    INX_SERIAL.printf("[%lu] [SCT] next chapter prefetch publish failed spine=%d pages=%u\n", millis(), spineIndex,
                      static_cast<unsigned>(pageCount));
    return fail();
  }
  // Progressive prefetch only builds section pages. Do not flush the shared
  // EPUB image metadata from the background worker; metadata generation may
  // be processing another book at the same time.
  progressiveParser_.reset();
  progressiveInput_.reset();
  progressiveFilePath_.clear();
  progressiveActive_ = false;
  return true;
}

/**
 * Loads a specific page from the section file.
 * Uses the look-up table to locate and deserialize the requested page.
 *
 * @return Section-owned page, or nullptr on failure
 */
Page* Section::loadPage(const int pageIndex) {
  // The foreground reader and the background chapter inflater share the SD/SPI
  // bus. Keep the complete seek/read/deserialize transaction together so the
  // active page can never observe an interleaved filesystem read.
  EpubImagePrefetch::IoLock ioLock;

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
