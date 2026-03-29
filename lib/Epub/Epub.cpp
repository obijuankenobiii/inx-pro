/**
 * @file Epub.cpp
 * @brief Definitions for Epub.
 */

#include "Epub.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <ZipFile.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <new>

#include "../../src/system/EpubPerf.h"
#include "../../src/util/StringUtils.h"
#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"
#include "Epub/ImagePrefetch.h"

namespace {

bool spineHrefLooksLikeRenderableHtml(const std::string& href) {
  if (href.empty()) {
    return false;
  }
  std::string h = href;
  const size_t hash = h.find('#');
  if (hash != std::string::npos) {
    h = h.substr(0, hash);
  }
  for (char& c : h) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  static const char* kImageExt[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp"};
  for (const char* ext : kImageExt) {
    const size_t n = std::strlen(ext);
    if (h.size() >= n && std::memcmp(h.c_str() + h.size() - n, ext, n) == 0) {
      return false;
    }
  }
  static const char* kHtmlExt[] = {".xhtml", ".html", ".htm", ".xht"};
  for (const char* ext : kHtmlExt) {
    const size_t n = std::strlen(ext);
    if (h.size() >= n && std::memcmp(h.c_str() + h.size() - n, ext, n) == 0) {
      return true;
    }
  }
  return false;
}

constexpr const char* kPackagedDeviceThumbnailPath = "META-INF/thumbnail.jpg";
constexpr const char* kBookMetadataCacheFile = "/book.bin";
constexpr int kThumbnailMaxWidth = 360;
constexpr int kThumbnailMaxHeight = 540;
constexpr uint8_t kThumbnailJpegQuality = 96;
// Keep this deliberately below the 8 MB PSRAM budget: thumbnail conversion
// also needs its own scaled output buffer, plus reader fonts/layout memory.
constexpr size_t kSlowPathCoverPsramMaxBytes = 1024 * 1024;
constexpr size_t kImageMetadataProbeBytes = 64 * 1024;
const std::string kEmptyString;

class PrefixSink final : public Print {
 public:
  PrefixSink(uint8_t* data, const size_t capacity) : data_(data), capacity_(capacity) {}

  size_t write(const uint8_t* data, const size_t size) override {
    if (!data_ || size > capacity_ - size_) return 0;
    memcpy(data_ + size_, data, size);
    size_ += size;
    return size;
  }
  size_t write(const uint8_t value) override { return write(&value, 1); }
  size_t size() const { return size_; }

 private:
  uint8_t* data_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
};

class FileAndPsramSink final : public Print {
 public:
  FileAndPsramSink(FsFile& file, uint8_t* bytes, const size_t capacity) : file_(file), bytes_(bytes), capacity_(capacity) {}

  size_t write(const uint8_t* data, const size_t size) override {
    if (!data || !bytes_ || written_ > capacity_ || size > capacity_ - written_) return 0;
    if (file_.write(data, size) != size) return 0;
    memcpy(bytes_ + written_, data, size);
    written_ += size;
    return size;
  }
  size_t write(const uint8_t value) override { return write(&value, 1); }
  size_t written() const { return written_; }

 private:
  FsFile& file_;
  uint8_t* bytes_ = nullptr;
  size_t capacity_ = 0;
  size_t written_ = 0;
};

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t readBe16(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
}

uint32_t readBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

bool probeImageHeader(const uint8_t* data, const size_t size, int* width, int* height) {
  if (!data || !width || !height) return false;
  *width = 0;
  *height = 0;

  if (size >= 24 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(data + 12, "IHDR", 4) == 0) {
    *width = static_cast<int>(readBe32(data + 16));
    *height = static_cast<int>(readBe32(data + 20));
    return *width > 0 && *height > 0;
  }
  if (size >= 26 && data[0] == 'B' && data[1] == 'M') {
    *width = static_cast<int>(readLe32(data + 18));
    *height = static_cast<int>(readLe32(data + 22));
    if (*height < 0) *height = -*height;
    return *width > 0 && *height > 0;
  }
  if (size >= 10 && memcmp(data, "GIF", 3) == 0) {
    *width = static_cast<int>(readLe16(data + 6));
    *height = static_cast<int>(readLe16(data + 8));
    return *width > 0 && *height > 0;
  }
  if (size < 4 || data[0] != 0xff || data[1] != 0xd8) return false;

  size_t pos = 2;
  while (pos + 4 <= size) {
    while (pos < size && data[pos] == 0xff) ++pos;
    if (pos >= size) break;
    const uint8_t marker = data[pos++];
    if (marker == 0xd8 || marker == 0xd9 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (pos + 2 > size) break;
    const uint16_t length = readBe16(data + pos);
    if (length < 2 || pos + length > size) break;
    const bool sof = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
    if (sof && length >= 7) {
      *height = static_cast<int>(readBe16(data + pos + 3));
      *width = static_cast<int>(readBe16(data + pos + 5));
      return *width > 0 && *height > 0;
    }
    pos += length;
  }
  return false;
}

}  // namespace

Epub::Epub(std::string filepath, const std::string& oldCacheDir) : filepath(std::move(filepath)) {
  (void)oldCacheDir;
  std::string hash = std::to_string(std::hash<std::string>{}(this->filepath));
  cachePath = "/.metadata/epub/" + hash;
}

Epub::~Epub() = default;

void Epub::clearCoverJpegPsram() const {
  std::vector<uint8_t, EpubPsramAllocator<uint8_t>> empty;
  coverJpegPsram_.swap(empty);
}

bool Epub::extractCoverJpegToPathAndPsram(const std::string& itemHref, const std::string& outPath) const {
  size_t sourceSize = 0;
  if (!getItemSize(itemHref, &sourceSize) || sourceSize == 0 || sourceSize > kSlowPathCoverPsramMaxBytes) {
    clearCoverJpegPsram();
    return extractItemToPath(itemHref, outPath, 16 * 1024);
  }

  clearCoverJpegPsram();
  try {
    coverJpegPsram_.resize(sourceSize);
  } catch (const std::bad_alloc&) {
    clearCoverJpegPsram();
    return extractItemToPath(itemHref, outPath, 16 * 1024);
  }
  if (coverJpegPsram_.empty()) {
    return extractItemToPath(itemHref, outPath, 16 * 1024);
  }

  EpubImagePrefetch::IoLock ioLock;
  FsFile out;
  if (!SdMan.openFileForWrite("EBP", outPath, out)) {
    clearCoverJpegPsram();
    return false;
  }
  FileAndPsramSink sink(out, coverJpegPsram_.data(), coverJpegPsram_.size());
  const bool ok = readItemContentsToStream(itemHref, sink, 16 * 1024) && sink.written() == sourceSize;
  out.sync();
  out.close();
  if (!ok) {
    clearCoverJpegPsram();
  }
  EPUB_PERF_LOG("[%lu] [THUMB-GEN] cover PSRAM source=%lu cached=%d\n", millis(),
                static_cast<unsigned long>(sourceSize), ok ? 1 : 0);
  return ok;
}

void Epub::loadPersistedImageMetadata() {
  imageMetadata_.clear();
  imageMetadataDirty_ = false;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return;
  }
  std::vector<BookMetadataCache::ImageMetadataEntry> stored;
  if (!bookMetadataCache->loadImageMetadata(stored)) {
    return;
  }
  imageMetadata_.reserve(stored.size());
  for (const auto& entry : stored) {
    if (entry.path.empty() || entry.width == 0 || entry.height == 0) continue;
    imageMetadata_.push_back(
        {EpubPsramString(entry.path.c_str()), static_cast<int>(entry.width), static_cast<int>(entry.height),
         entry.format, entry.valid});
  }
}

ZipFile& Epub::zip() const {
  if (!zipFile_) {
    zipFile_ = std::make_unique<ZipFile>(filepath);
  }
  if (!zipIndexAttempted_) {
    zipIndexAttempted_ = true;
    const bool indexed = zipFile_->loadAllFileStatSlims();
    EPUB_PERF_LOG("[%lu] [EBP-ZIP] index %s entries=%u path=%s\n", millis(), indexed ? "ready" : "fallback",
                  indexed ? static_cast<unsigned>(zipFile_->entryCount()) : 0u, filepath.c_str());
  }
  return *zipFile_;
}

ZipFile& Epub::nestedZip() const {
  if (!nestedZipFile_) {
    nestedZipFile_ = std::make_unique<ZipFile>(filepath);
  }
  if (!nestedZipIndexAttempted_) {
    nestedZipIndexAttempted_ = true;
    const bool indexed = nestedZipFile_->loadAllFileStatSlims();
    EPUB_PERF_LOG("[%lu] [EBP-ZIP] nested index %s entries=%u path=%s\n", millis(), indexed ? "ready" : "fallback",
                  indexed ? static_cast<unsigned>(nestedZipFile_->entryCount()) : 0u, filepath.c_str());
  }
  return *nestedZipFile_;
}

ZipFile& Epub::zipForNestedOperation() const {
  return zipStreamDepth_ == 0 ? zip() : nestedZip();
}

/**
 * @brief Checks file type is png.
 */
static bool isPngFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".png"); }

/**
 * @brief Checks file type is jpeg.
 */
static bool isJpegFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg");
}

/**
 * @brief Checks file type is bmp.
 */
static bool isBmpFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".bmp"); }

static bool isGifFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".gif"); }

static bool isSupportedInBodyImageFile(const std::string& path) {
  return isBmpFile(path) || isPngFile(path) || isJpegFile(path) || isGifFile(path);
}

/**
 * @brief Creates the cache directory structure for this EPUB.
 *
 * Creates both the main cache directory and the images subdirectory
 * if they don't already exist on the SD card.
 */
void Epub::setupCacheDir() const {
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  std::string imagesPath = cachePath + "/images";
  if (!SdMan.exists(imagesPath.c_str())) {
    SdMan.mkdir(imagesPath.c_str());
  }
}

/**
 * @brief Generates a cache file path for an internal image reference.
 *
 * @param internalHref Internal EPUB path to the image file
 * @return Full filesystem path where the converted BMP should be cached
 */
std::string Epub::getCacheImgPath(const std::string& internalHref) const {
  size_t lastSlash = internalHref.find_last_of('/');
  std::string fileName = (lastSlash == std::string::npos) ? internalHref : internalHref.substr(lastSlash + 1);

  size_t dot = fileName.find_last_of('.');
  if (dot != std::string::npos) {
    fileName = fileName.substr(0, dot);
  }
  if (isJpegFile(internalHref)) {
    return cachePath + "/images/" + fileName + ".jpg";
  }
  if (isGifFile(internalHref)) {
    return cachePath + "/images/" + fileName + ".gif";
  }
  if (isPngFile(internalHref)) {
    return cachePath + "/images/" + fileName + ".png";
  }
  return cachePath + "/images/" + fileName + ".bmp";
}

bool Epub::extractItemToPath(const std::string& itemHref, const std::string& outPath, const size_t chunkSize) const {
  EpubImagePrefetch::IoLock ioLock;
  FsFile out;
  if (!SdMan.openFileForWrite("EBP", outPath, out)) {
    return false;
  }
  const bool ok = readItemContentsToStream(itemHref, out, chunkSize);
  out.sync();
  out.close();
  return ok;
}

/**
 * @brief Reads the contents of an EPUB internal file to an output stream.
 *
 * @param itemHref Internal path to the file within the EPUB
 * @param out Output stream to write the file contents to
 * @param chunkSize Size of chunks to read at a time
 * @return true if the file was successfully read, false otherwise
 */
bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    const size_t maxOutputBytes) const {
  EpubImagePrefetch::IoLock ioLock;
  if (itemHref.empty()) return false;

  const std::string path = FsHelpers::normalisePath(itemHref);

  EPUB_PERF_LOG("[EBP] Zip Request: %s\n", path.c_str());

  // Select before increasing depth: the outer chapter stream owns the primary
  // ZIP handle; a SAX callback uses the independent nested handle.
  ZipFile& reader = zipForNestedOperation();
  struct StreamDepthScope {
    explicit StreamDepthScope(const Epub& epub) : epub_(epub) { ++epub_.zipStreamDepth_; }
    ~StreamDepthScope() { --epub_.zipStreamDepth_; }
    const Epub& epub_;
  } streamDepth(*this);
  return reader.readFileToStream(path.c_str(), out, chunkSize, maxOutputBytes);
}

struct Epub::ItemStream::Impl {
  Impl(const Epub& epubIn, std::unique_ptr<ZipFile::Stream> streamIn) : epub(epubIn), stream(std::move(streamIn)) {
    ++epub.zipStreamDepth_;
  }

  ~Impl() {
    if (epub.zipStreamDepth_ > 0) {
      --epub.zipStreamDepth_;
    }
  }

  const Epub& epub;
  std::unique_ptr<ZipFile::Stream> stream;
};

Epub::ItemStream::ItemStream(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Epub::ItemStream::~ItemStream() = default;

Epub::ItemStream::Result Epub::ItemStream::pump(Print& out, const size_t maxOutputBytes) {
  if (!impl_ || !impl_->stream) {
    return Result::Error;
  }

  // Do not keep the recursive SD mutex across reader-loop slices. The stream
  // owns only its ZipFile cursor; nested EPUB operations use the secondary
  // handle while this scope serializes the actual SPI transaction.
  EpubImagePrefetch::IoLock ioLock;
  switch (impl_->stream->pump(out, maxOutputBytes)) {
    case ZipFile::Stream::Result::More:
      return Result::More;
    case ZipFile::Stream::Result::Done:
      return Result::Done;
    case ZipFile::Stream::Result::Error:
      return Result::Error;
  }
  return Result::Error;
}

bool Epub::ItemStream::finished() const { return impl_ && impl_->stream && impl_->stream->finished(); }

std::unique_ptr<Epub::ItemStream> Epub::openItemStream(const std::string& itemHref) const {
  if (itemHref.empty()) {
    return nullptr;
  }

  EpubImagePrefetch::IoLock ioLock;
  const std::string path = FsHelpers::normalisePath(itemHref);
  ZipFile& reader = zipForNestedOperation();
  std::unique_ptr<ZipFile::Stream> stream = reader.openStream(path.c_str());
  if (!stream) {
    return nullptr;
  }
  return std::unique_ptr<ItemStream>(new ItemStream(std::make_unique<ItemStream::Impl>(*this, std::move(stream))));
}

/**
 * @brief Extracts an image from the EPUB and converts it for in-body rendering.
 *
 * BMP sources are copied as-is. PNG/JPEG use the same 2-bit Floyd–Steinberg pipeline as the web
 * Files page (contain within 500×820, BT.601 rounded luma, palette 0/85/170/255, pack thresholds
 * 42/127/212). Thumbnails, covers, and full-screen extraction use other entry points.
 *
 * @param itemHref Internal path to the image file
 * @param outBmpPath Output path for the converted BMP file
 * @param targetW Ignored for PNG/JPEG (kept for API compatibility with callers)
 * @param targetH Ignored for PNG/JPEG
 * @return true if extraction and conversion succeeded, false otherwise
 */
bool Epub::extractAndConvertImage(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                  int targetH) const {
  EpubImagePrefetch::IoLock ioLock;
  EPUB_PERF_LOG("[%lu] [EBP-IMG] extract start href=%s out=%s\n", static_cast<unsigned long>(millis()),
                itemHref.c_str(), outBmpPath.c_str());

  if (!isSupportedInBodyImageFile(itemHref)) {
    INX_SERIAL.printf("[%lu] [EBP-IMG] unsupported in-body image format: %s\n", static_cast<unsigned long>(millis()),
                  itemHref.c_str());
    return false;
  }

  // Preparation is serialized with all other EPUB SD work. The temporary name
  // still remains unique so a failed write can never publish a partial image.
  const std::string tempPath = cachePath + "/.extract_" +
                               std::to_string(std::hash<std::string>{}(itemHref + outBmpPath)) + ".tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    INX_SERIAL.printf("[%lu] [EBP-IMG] open temp write fail: %s\n", static_cast<unsigned long>(millis()), tempPath.c_str());
    return false;
  }

  bool extracted = readItemContentsToStream(itemHref, tempFile, 16 * 1024);
  tempFile.flush();
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    INX_SERIAL.printf("[%lu] [EBP-IMG] zip read failed href=%s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    INX_SERIAL.printf("[%lu] [EBP-IMG] open temp read fail: %s\n", static_cast<unsigned long>(millis()), tempPath.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  sourceFile.seek(0);
  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    INX_SERIAL.printf("[%lu] [EBP-IMG] open out bmp write fail: %s\n", static_cast<unsigned long>(millis()),
                  outBmpPath.c_str());
    sourceFile.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = false;

  if (isBmpFile(itemHref) || isGifFile(itemHref)) {
    // GIF is cached raw, exactly like JPEG/PNG: getImagePath() keeps the .gif extension and
    // ImageRender decodes it at render time (stb, streamed, PSRAM-transient). No conversion here, so
    // nothing extra is written to the card and no decode buffer outlives this call.
    EPUB_PERF_LOG("[%lu] [EBP-IMG] raw copy: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[16 * 1024]);
    if (!buf) {
      sourceFile.close();
      destFile.close();
      SdMan.remove(tempPath.c_str());
      SdMan.remove(outBmpPath.c_str());
      return false;
    }
    while (sourceFile.available()) {
      size_t r = sourceFile.read(buf.get(), 16 * 1024);
      destFile.write(buf.get(), r);
    }
    success = true;
  } else if (isPngFile(itemHref)) {
    (void)targetW;
    (void)targetH;
    EPUB_PERF_LOG("[%lu] [EBP-IMG] PNG convert: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    success = PngToBmpConverter::pngFileToEpubWebStyle2BitBmpStream(sourceFile, destFile);
    if (!success) {
      INX_SERIAL.printf("[%lu] [EBP-IMG] PNG pipeline failed: %s\n", static_cast<unsigned long>(millis()),
                    itemHref.c_str());
    }
  } else if (isJpegFile(itemHref)) {
    (void)targetW;
    (void)targetH;
    EPUB_PERF_LOG("[%lu] [EBP-IMG] JPEG convert: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    success = JpegToBmpConverter::jpegFileToEpubWebStyle2BitBmpStream(sourceFile, destFile);
    if (!success) {
      INX_SERIAL.printf("[%lu] [EBP-IMG] JPEG pipeline failed: %s\n", static_cast<unsigned long>(millis()),
                    itemHref.c_str());
    }
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  if (success) {
    EPUB_PERF_LOG("[%lu] [EBP-IMG] extract ok -> %s\n", static_cast<unsigned long>(millis()), outBmpPath.c_str());
  } else {
    INX_SERIAL.printf("[%lu] [EBP-IMG] extract failed (after convert) href=%s\n", static_cast<unsigned long>(millis()),
                  itemHref.c_str());
    SdMan.remove(outBmpPath.c_str());
  }

  return success;
}

/**
 * @brief Extracts an image and centers it on a full-screen canvas.
 *
 * For BMP sources, the file is copied directly without conversion.
 * For PNG and JPEG sources, the image is centered on the target canvas.
 *
 * @param itemHref Internal path to the image file
 * @param outBmpPath Output path for the converted BMP file
 * @param targetW Target canvas width (max width for contain mode)
 * @param targetH Target canvas height (max height for contain mode)
 * @param cropToFill true = cover (fill target size, center crop); false = contain (whole image, fit in target)
 * @return true if extraction and conversion succeeded, false otherwise
 */
bool Epub::extractAndConvertImageFullScreen(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                            int targetH, bool cropToFill) const {
  EpubImagePrefetch::IoLock ioLock;
  if (itemHref.empty()) {
    return false;
  }

  const std::string tempPath = cachePath + "/.extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    return false;
  }

  bool extracted = readItemContentsToStream(itemHref, tempFile, 2048);
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    sourceFile.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = false;

  if (isBmpFile(itemHref)) {
    if (targetW > 0 && targetH > 0) {
      success = JpegToBmpConverter::resizeBitmap(sourceFile, destFile, targetW, targetH);
      if (!success) {
        sourceFile.seek(0);
        destFile.close();
        SdMan.remove(outBmpPath.c_str());
        if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
          sourceFile.close();
          SdMan.remove(tempPath.c_str());
          return false;
        }
        INX_SERIAL.printf("[EBP] BMP resize failed, copying raw cover: %s\n", itemHref.c_str());
        uint8_t buf[2048];
        while (sourceFile.available()) {
          size_t r = sourceFile.read(buf, sizeof(buf));
          destFile.write(buf, r);
        }
        success = true;
      }
    } else {
      INX_SERIAL.printf("[EBP] Source is already BMP for cover, copying directly: %s\n", itemHref.c_str());
      uint8_t buf[2048];
      while (sourceFile.available()) {
        size_t r = sourceFile.read(buf, sizeof(buf));
        destFile.write(buf, r);
      }
      success = true;
    }
  } else if (isPngFile(itemHref)) {
    success = PngToBmpConverter::pngFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH, cropToFill);
  } else {
    success = JpegToBmpConverter::jpegFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH, cropToFill);
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}

/**
 * @brief Generates the cover image as a BMP file.
 *
 * @param cropped If true, generates a cropped cover; if false, full cover
 * @return true if cover generation succeeded, false otherwise
 */
bool Epub::generateCoverBmp(bool cropped) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->coreMetadata.coverItemHref.empty()) {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] cover generation skipped metadata=%d href=%s book=%s\n", millis(),
                  bookMetadataCache && bookMetadataCache->isLoaded(),
                  bookMetadataCache ? bookMetadataCache->coreMetadata.coverItemHref.c_str() : "<none>",
                  filepath.c_str());
    return false;
  }
  const std::string& coverHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (isJpegFile(coverHref)) {
    SdMan.remove(getCoverBmpPath(cropped).c_str());
    const bool success = cropped ? extractItemToPath(coverHref, getCoverJpegPath(cropped), 16 * 1024)
                                 : extractCoverJpegToPathAndPsram(coverHref, getCoverJpegPath(false));
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] cover generation result=%d type=jpeg href=%s output=%s exists=%d\n", millis(),
                  success, coverHref.c_str(), getCoverJpegPath(cropped).c_str(),
                  SdMan.exists(getCoverJpegPath(cropped).c_str()));
    return success;
  }
  SdMan.remove(getCoverJpegPath(cropped).c_str());
  const bool success = extractAndConvertImageFullScreen(bookMetadataCache->coreMetadata.coverItemHref,
                                                       getCoverBmpPath(cropped), 480, 800, cropped);
  INX_SERIAL.printf("[%lu] [THUMB-TRACE] cover generation result=%d type=bitmap href=%s output=%s exists=%d\n", millis(),
                success, coverHref.c_str(), getCoverBmpPath(cropped).c_str(),
                SdMan.exists(getCoverBmpPath(cropped).c_str()));
  return success;
}

/**
 * @brief Builds cache thumbnails: prefers a packaged `META-INF/thumbnail.jpg` from the EPUB, else generates a
 * resized `thumb.jpg` or `thumb.bmp` from the cover.
 */
bool Epub::generateThumbBmp(const bool skipCoverFallback) const {
  INX_SERIAL.printf("[%lu] [THUMB-GEN] ENTER book=%s cache=%s skipFallback=%d\n", millis(), filepath.c_str(),
                 cachePath.c_str(), skipCoverFallback ? 1 : 0);
  const auto cachedFileSize = [](const std::string& path) -> uint32_t {
    FsFile file;
    if (!SdMan.openFileForRead("EBP", path, file)) {
      return 0;
    }
    const uint32_t size = static_cast<uint32_t>(file.size());
    file.close();
    return size;
  };
  const std::string thumbJpegPath = getThumbJpegPath();
  const std::string thumbPngPath = getThumbPngPath();
  const std::string thumbBmpPath = getThumbBmpPath();
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->coreMetadata.coverItemHref.empty()) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB thumb has no usable metadata loaded=%d cover=%s book=%s\n", millis(),
                  bookMetadataCache && bookMetadataCache->isLoaded(),
                  bookMetadataCache ? bookMetadataCache->coreMetadata.coverItemHref.c_str() : "<none>",
                  filepath.c_str());
    return false;
  }

  const std::string& coverHref = bookMetadataCache->coreMetadata.coverItemHref;
  const bool bitmapThumbnail = isPngFile(coverHref) || isBmpFile(coverHref);
  INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB thumb begin cover=%s type=%s skipFallback=%d cache=%s\n", millis(),
                coverHref.c_str(), bitmapThumbnail ? (isPngFile(coverHref) ? "png" : "bmp") : "jpeg",
                skipCoverFallback, cachePath.c_str());
  if (bitmapThumbnail) {
    if (SdMan.exists(thumbBmpPath.c_str())) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB bitmap thumb already exists: %s\n", millis(), thumbBmpPath.c_str());
      return true;
    }
    // The slow path has already converted the PNG/BMP cover to cover.bmp. Resize that cached BMP
    // directly so thumbnail generation uses the same successful conversion instead of decoding the EPUB entry again.
    bool success = false;
    const std::string cachedCoverBmpPath = getCoverBmpPath(false);
    if (isPngFile(coverHref)) {
      // Re-decode PNG so the thumbnail gets the same improved 1-bit Atkinson treatment,
      // even when an older, darker cover.bmp is already cached.
      success = extractAndConvertImageFullScreen(coverHref, thumbBmpPath, kThumbnailMaxWidth, kThumbnailMaxHeight,
                                                 false);
    } else {
      FsFile cachedCoverFile;
      FsFile thumbFile;
      if (SdMan.exists(cachedCoverBmpPath.c_str()) &&
          SdMan.openFileForRead("EBP", cachedCoverBmpPath, cachedCoverFile) &&
          SdMan.openFileForWrite("EBP", thumbBmpPath, thumbFile)) {
        success =
            JpegToBmpConverter::resizeBitmap(cachedCoverFile, thumbFile, kThumbnailMaxWidth, kThumbnailMaxHeight);
        thumbFile.sync();
        thumbFile.close();
        cachedCoverFile.close();
      } else {
        if (cachedCoverFile) cachedCoverFile.close();
        if (thumbFile) thumbFile.close();
      }
    }
    if (!success && SdMan.exists(cachedCoverBmpPath.c_str())) {
      FsFile sourceFile;
      FsFile outputFile;
      if (SdMan.openFileForRead("EBP", cachedCoverBmpPath, sourceFile) &&
          SdMan.openFileForWrite("EBP", thumbBmpPath, outputFile)) {
        uint8_t buffer[2048];
        while (sourceFile.available()) {
          const size_t bytesRead = sourceFile.read(buffer, sizeof(buffer));
          if (bytesRead == 0 || outputFile.write(buffer, bytesRead) != bytesRead) {
            break;
          }
        }
        outputFile.sync();
        outputFile.close();
        sourceFile.close();
        success = SdMan.exists(thumbBmpPath.c_str());
        INX_SERIAL.printf("[%lu] [THUMB-GEN] PNG fallback copied cover.bmp to thumb.bmp success=%d\n", millis(),
                       success ? 1 : 0);
      } else {
        if (sourceFile) sourceFile.close();
        if (outputFile) outputFile.close();
      }
    }
    if (success) {
      SdMan.remove(thumbJpegPath.c_str());
      SdMan.remove(thumbPngPath.c_str());
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB bitmap thumb ok source=%s output=%s bytes=%lu\n", millis(),
                    coverHref.c_str(), thumbBmpPath.c_str(),
                    static_cast<unsigned long>(cachedFileSize(thumbBmpPath)));
      return true;
    }
    SdMan.remove(thumbBmpPath.c_str());
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB bitmap thumb conversion failed source=%s output=%s\n", millis(),
                  coverHref.c_str(), thumbBmpPath.c_str());
    return false;
  } else if (SdMan.exists(thumbJpegPath.c_str()) || SdMan.exists(thumbPngPath.c_str()) ||
             SdMan.exists(thumbBmpPath.c_str())) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB thumb already exists jpg=%d png=%d bmp=%d\n", millis(),
                  SdMan.exists(thumbJpegPath.c_str()), SdMan.exists(thumbPngPath.c_str()),
                  SdMan.exists(thumbBmpPath.c_str()));
    return true;
  }

  size_t packagedThumbSize = 0;
  if (getItemSize(kPackagedDeviceThumbnailPath, &packagedThumbSize) && packagedThumbSize > 0) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] packaged thumbnail found bytes=%lu path=%s\n", millis(),
                  static_cast<unsigned long>(packagedThumbSize), kPackagedDeviceThumbnailPath);
    if (extractItemToPath(kPackagedDeviceThumbnailPath, thumbJpegPath, 2048)) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] packaged thumbnail extracted output=%s\n", millis(), thumbJpegPath.c_str());
      return true;
    }
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] packaged thumbnail extract failed path=%s\n", millis(),
                  kPackagedDeviceThumbnailPath);
  }

  if (skipCoverFallback) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] thumbnail cover fallback skipped because prior cover extraction failed: %s\n",
                  millis(), coverHref.c_str());
    return false;
  }

  if (!isJpegFile(coverHref)) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] thumbnail fallback skipped for unsupported cover format: %s coverBmp=%d coverJpeg=%d\n",
                  millis(), coverHref.c_str(), SdMan.exists(getCoverBmpPath(false).c_str()),
                  SdMan.exists(getCoverJpegPath(false).c_str()));
    return false;
  }

  // generateCoverBmp() already extracted this exact JPEG entry as a raw copy at getCoverJpegPath() when it
  // succeeded - reuse it instead of extracting the same zip entry a second time.
  const std::string cachedCoverJpegPath = getCoverJpegPath(false);
  const bool haveCachedCover = SdMan.exists(cachedCoverJpegPath.c_str());
  const bool usePsramCover = haveCachedCover && !coverJpegPsram_.empty();
  const std::string tempPath = cachePath + "/.thumb_extract.tmp";

  FsFile sourceFile;
  if (haveCachedCover && !usePsramCover) {
    if (!SdMan.openFileForRead("EBP", cachedCoverJpegPath, sourceFile)) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] cached cover open failed: %s\n", millis(), cachedCoverJpegPath.c_str());
      return false;
    }
  } else {
    FsFile tempFile;
    if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] temp cover open for write failed: %s\n", millis(), tempPath.c_str());
      return false;
    }
    const bool extracted = readItemContentsToStream(coverHref, tempFile, 2048);
    tempFile.sync();
    tempFile.close();
    if (!extracted) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] cover extraction failed href=%s temp=%s\n", millis(), coverHref.c_str(),
                    tempPath.c_str());
      SdMan.remove(tempPath.c_str());
      return false;
    }
    if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] extracted cover reopen failed: %s\n", millis(), tempPath.c_str());
      SdMan.remove(tempPath.c_str());
      return false;
    }
  }

  bool success;
  FsFile thumbFile;
  if (SdMan.openFileForWrite("EBP", thumbJpegPath, thumbFile)) {
    JpegToBmpConverter converter;
    success = usePsramCover
                  ? converter.jpegBytesToThumbnailJpeg(coverJpegPsram_.data(), coverJpegPsram_.size(), thumbFile,
                                                       kThumbnailMaxWidth, kThumbnailMaxHeight, kThumbnailJpegQuality)
                  : converter.jpegFileToThumbnailJpeg(sourceFile, thumbFile, kThumbnailMaxWidth, kThumbnailMaxHeight,
                                                      kThumbnailJpegQuality);
    thumbFile.sync();
    thumbFile.close();
    // PSRAM uses the bounded PicoJPEG path. Progressive or unusual JPEGs are
    // intentionally retried through the established file/STB path rather than
    // sacrificing thumbnail compatibility for the faster common case.
    if (!success && usePsramCover) {
      SdMan.remove(thumbJpegPath.c_str());
      FsFile diskCover;
      FsFile retryThumb;
      if (SdMan.openFileForRead("EBP", cachedCoverJpegPath, diskCover) &&
          SdMan.openFileForWrite("EBP", thumbJpegPath, retryThumb)) {
        success = converter.jpegFileToThumbnailJpeg(diskCover, retryThumb, kThumbnailMaxWidth, kThumbnailMaxHeight,
                                                     kThumbnailJpegQuality);
        retryThumb.sync();
      }
      if (retryThumb) retryThumb.close();
      if (diskCover) diskCover.close();
    }
    if (!success) {
      SdMan.remove(thumbJpegPath.c_str());
    }
  } else {
    success = false;
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] thumbnail output open failed: %s\n", millis(), thumbJpegPath.c_str());
  }
  const uint32_t outputBytes = SdMan.exists(thumbJpegPath.c_str()) ? cachedFileSize(thumbJpegPath) : 0u;
  INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB JPEG thumbnail resize %s output=%s bytes=%lu source=%s\n", millis(),
                success ? "ok" : "failed", thumbJpegPath.c_str(), static_cast<unsigned long>(outputBytes),
                usePsramCover ? "PSRAM" : (haveCachedCover ? cachedCoverJpegPath.c_str() : tempPath.c_str()));

  if (sourceFile) sourceFile.close();
  if (!haveCachedCover) {
    SdMan.remove(tempPath.c_str());
  }

  return success;
}

/**
 * @brief Finds and parses the container.xml file to locate the OPF file.
 *
 * @param contentOpfFile Output parameter for the OPF file path
 * @return true if the container.xml was found and parsed successfully
 */
bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;
  if (!getItemSize(containerPath, &containerSize)) return false;
  ContainerParser containerParser(containerSize);
  if (!containerParser.setup() || !readItemContentsToStream(containerPath, containerParser, 512)) return false;
  if (containerParser.fullPath.empty()) return false;
  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

/**
 * @brief Parses the OPF file to extract book metadata.
 *
 * @param bookMetadata Reference to store the parsed metadata
 * @return true if parsing succeeded, false otherwise
 */
bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata) {
  std::string opfPath;
  if (!findContentOpfFile(&opfPath)) return false;
  contentBasePath = opfPath.substr(0, opfPath.find_last_of('/') + 1);

  size_t opfSize;
  if (!getItemSize(opfPath, &opfSize)) return false;

  ContentOpfParser opfParser(getCachePath(), getBasePath(), opfSize, bookMetadataCache.get());
  if (!opfParser.setup() || !readItemContentsToStream(opfPath, opfParser, 1024)) return false;

  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;
  bookMetadata.textReferenceHref = opfParser.textReferenceHref;
  if (!opfParser.tocNcxPath.empty()) tocNcxItem = opfParser.tocNcxPath;
  if (!opfParser.tocNavPath.empty()) tocNavItem = opfParser.tocNavPath;

  return true;
}

/**
 * @brief Parses the NCX table of contents file.
 *
 * @return true if parsing succeeded, false otherwise
 */
bool Epub::parseTocNcxFile() const {
  if (tocNcxItem.empty()) return false;
  const auto tmp = cachePath + "/toc.ncx";
  FsFile f;
  if (!SdMan.openFileForWrite("EBP", tmp, f)) return false;
  readItemContentsToStream(tocNcxItem, f, 1024);
  f.close();
  if (!SdMan.openFileForRead("EBP", tmp, f)) return false;
  TocNcxParser parser(contentBasePath, f.size(), bookMetadataCache.get());
  if (!parser.setup()) {
    f.close();
    return false;
  }
  uint8_t buf[1024];
  while (f.available()) {
    size_t r = f.read(buf, 1024);
    if (parser.write(buf, r) != r) break;
  }
  f.close();
  SdMan.remove(tmp.c_str());
  return true;
}

/**
 * @brief Parses the NAV table of contents file (EPUB3).
 *
 * @return true if parsing succeeded, false otherwise
 */
bool Epub::parseTocNavFile() const {
  if (tocNavItem.empty()) return false;
  const auto tmp = cachePath + "/toc.nav";
  FsFile f;
  if (!SdMan.openFileForWrite("EBP", tmp, f)) return false;
  readItemContentsToStream(tocNavItem, f, 1024);
  f.close();
  if (!SdMan.openFileForRead("EBP", tmp, f)) return false;
  const std::string navBase = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser parser(navBase, f.size(), bookMetadataCache.get());
  if (!parser.setup()) {
    f.close();
    return false;
  }
  uint8_t buf[1024];
  while (f.available()) {
    size_t r = f.read(buf, 1024);
    if (parser.write(buf, r) != r) break;
  }
  f.close();
  SdMan.remove(tmp.c_str());
  return true;
}

/**
 * @brief Loads the EPUB book and builds metadata cache if needed.
 *
 * @param buildIfMissing If true, builds the cache when not present
 * @return true if the book was successfully loaded, false otherwise
 */
bool Epub::load(const bool buildIfMissing) {
  setupCacheDir();

  // A single Epub instance represents one opened book. Recreate the
  // per-session ZIP index and image metadata when it is loaded again so no
  // offsets or dimensions from a previous session survive.
  zipFile_.reset();
  zipIndexAttempted_ = false;
  nestedZipFile_.reset();
  nestedZipIndexAttempted_ = false;
  zipStreamDepth_ = 0;
  clearCoverJpegPsram();
  imageMetadata_.clear();
  imageMetadataDirty_ = false;
  parsedCssParser_.reset();
  parsedCssLoaded_ = false;
  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  if (bookMetadataCache->load()) {
    loadPersistedImageMetadata();
    return true;
  }

  if (!buildIfMissing) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB metadata cache missing and build disabled: %s\n", millis(), filepath.c_str());
    return false;
  }

  if (!bookMetadataCache->beginWrite()) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB metadata cache beginWrite failed cache=%s book=%s\n", millis(),
                  cachePath.c_str(), filepath.c_str());
    return false;
  }

  BookMetadataCache::BookMetadata meta;
  bookMetadataCache->beginContentOpfPass();

  std::string opfPath;
  if (!findContentOpfFile(&opfPath)) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed: META-INF/container.xml or OPF not found book=%s\n", millis(),
                  filepath.c_str());
    return false;
  }
  contentBasePath = opfPath.substr(0, opfPath.find_last_of('/') + 1);

  size_t opfSize;
  if (!getItemSize(opfPath, &opfSize)) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed: OPF entry size unavailable opf=%s book=%s\n", millis(),
                  opfPath.c_str(), filepath.c_str());
    return false;
  }

  ContentOpfParser opfParser(cachePath, getBasePath(), opfSize, bookMetadataCache.get());
  if (!opfParser.setup()) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed: OPF parser setup failed opf=%s\n", millis(), opfPath.c_str());
    return false;
  }
  if (!readItemContentsToStream(opfPath, opfParser, 1024)) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed: OPF read failed opf=%s book=%s\n", millis(), opfPath.c_str(),
                  filepath.c_str());
    return false;
  }

  meta.title = opfParser.title;
  meta.author = opfParser.author;
  meta.language = opfParser.language;
  meta.coverItemHref = opfParser.coverItemHref;
  meta.textReferenceHref = opfParser.textReferenceHref;
  if (!opfParser.tocNcxPath.empty()) tocNcxItem = opfParser.tocNcxPath;
  if (!opfParser.tocNavPath.empty()) tocNavItem = opfParser.tocNavPath;

  bookMetadataCache->endContentOpfPass();

  bookMetadataCache->beginCssPass();
  bool cssExtracted = false;
  try {
    cssExtracted = bookMetadataCache->extractAndCacheCssFiles(filepath);
  } catch (const std::bad_alloc&) {
    INX_SERIAL.printf("[EBP] Warning: Skipping CSS extraction due to low heap\n");
  } catch (...) {
    INX_SERIAL.printf("[EBP] Warning: Skipping CSS extraction due to parser error\n");
  }
  if (!cssExtracted) {
    INX_SERIAL.printf("[EBP] Warning: Failed to extract CSS files\n");
  }
  bookMetadataCache->endCssPass();

  bookMetadataCache->beginTocPass();
  bool tocParsed = (!tocNavItem.empty()) ? parseTocNavFile() : false;
  if (!tocParsed && !tocNcxItem.empty()) tocParsed = parseTocNcxFile();
  bookMetadataCache->appendSyntheticTocFromSpineIfEmpty();
  bookMetadataCache->endTocPass();

  bookMetadataCache->endWrite();
  bookMetadataCache->buildBookBin(filepath, meta);
  bookMetadataCache->cleanupTmpFiles();
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  const bool loaded = bookMetadataCache->load();
  if (loaded) {
    loadPersistedImageMetadata();
  }
  if (!loaded) {
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed: rebuilt metadata cache could not be reopened cache=%s book=%s\n",
                  millis(), cachePath.c_str(), filepath.c_str());
  }
  return loaded;
}

bool Epub::hasMetadataCache() const { return SdMan.exists((cachePath + kBookMetadataCacheFile).c_str()); }

bool Epub::isLoaded() const { return bookMetadataCache && bookMetadataCache->isLoaded(); }

/**
 * @brief Clears all cached data for this EPUB.
 *
 * @return true if the cache was successfully cleared, false otherwise
 */
bool Epub::clearCache() {
  parsedCssParser_.reset();
  parsedCssLoaded_ = false;
  clearCoverJpegPsram();
  imageMetadata_.clear();
  imageMetadataDirty_ = false;
  if (bookMetadataCache) {
    bookMetadataCache.reset();
  }

  if (SdMan.exists(cachePath.c_str())) {
    return SdMan.removeDir(cachePath.c_str());
  }
  return true;
}

/**
 * @brief Gets the cache directory path for this EPUB.
 *
 * @return Full filesystem path to the cache directory
 */
const std::string& Epub::getCachePath() const { return cachePath; }

/**
 * @brief Gets the original EPUB file path.
 *
 * @return Full filesystem path to the EPUB file
 */
const std::string& Epub::getPath() const { return filepath; }

/**
 * @brief Gets the book title.
 *
 * @return Book title string, empty if not loaded
 */
const std::string& Epub::getTitle() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.title : kEmptyString;
}

/**
 * @brief Gets the book author.
 *
 * @return Author name string, empty if not loaded
 */
const std::string& Epub::getAuthor() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.author : kEmptyString;
}

/**
 * @brief Gets the book language.
 *
 * @return Language code string, empty if not loaded
 */
const std::string& Epub::getLanguage() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.language : kEmptyString;
}

/**
 * @brief Gets the filesystem path for the cover BMP.
 *
 * @param cropped If true, returns path for cropped cover; if false, full cover
 * @return Full filesystem path to the cover BMP file
 */
std::string Epub::getCoverBmpPath(bool cropped) const {
  return cachePath + (cropped ? "/cover_crop.bmp" : "/cover.bmp");
}

std::string Epub::getCoverJpegPath(bool cropped) const {
  return cachePath + (cropped ? "/cover_crop.jpg" : "/cover.jpg");
}

std::string Epub::getCoverItemHref() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return "";
  }
  return bookMetadataCache->coreMetadata.coverItemHref;
}

bool Epub::extractCoverItemToPath(const std::string& outPath) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return false;
  }
  const std::string& href = bookMetadataCache->coreMetadata.coverItemHref;
  if (href.empty()) {
    return false;
  }
  FsFile out;
  if (!SdMan.openFileForWrite("EBP", outPath, out)) {
    return false;
  }
  const bool ok = readItemContentsToStream(href, out, 2048);
  out.sync();
  out.close();
  return ok;
}

/**
 * @brief Gets the filesystem path for the thumbnail BMP.
 *
 * @return Full filesystem path to the thumbnail BMP file
 */
std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

std::string Epub::getThumbJpegPath() const { return cachePath + "/thumb.jpg"; }

std::string Epub::getThumbPngPath() const { return cachePath + "/thumb.png"; }

/**
 * @brief Gets the filesystem path for the small thumbnail BMP.
 *
 * @return Full filesystem path to the small thumbnail BMP file
 */
std::string Epub::getSmallThumbBmpPath() const { return cachePath + "/small_thumb.bmp"; }

/**
 * @brief Retrieves the size of an internal EPUB file.
 *
 * @param href Internal path to the file
 * @param size Output parameter for the file size
 * @return true if the size was successfully retrieved, false otherwise
 */
bool Epub::getItemSize(const std::string& href, size_t* size) const {
  return zipForNestedOperation().getInflatedFileSize(FsHelpers::normalisePath(href).c_str(), size);
}

bool Epub::probeImageDimensions(const std::string& itemHref, int* width, int* height) const {
  if (!width || !height || itemHref.empty()) return false;
  *width = 0;
  *height = 0;

  std::unique_ptr<uint8_t[]> prefix(new (std::nothrow) uint8_t[kImageMetadataProbeBytes]);
  if (!prefix) return false;
  PrefixSink sink(prefix.get(), kImageMetadataProbeBytes);
  if (!readItemContentsToStream(itemHref, sink, 16 * 1024, kImageMetadataProbeBytes)) return false;
  return probeImageHeader(prefix.get(), sink.size(), width, height);
}

bool Epub::getImageMetadata(const std::string& path, int* width, int* height, const uint8_t format) const {
  for (const ImageMetadata& item : imageMetadata_) {
    if (item.path.compare(path.c_str()) == 0 && item.format == format) {
      if (width) *width = item.width;
      if (height) *height = item.height;
      // A false entry is still a cache hit. The caller can decide whether to
      // remove/rebuild the file, but must not reopen it just to rediscover the
      // same invalid dimensions on every layout pass.
      return true;
    }
  }
  return false;
}

void Epub::setImageMetadata(const std::string& path, const int width, const int height, const uint8_t format,
                            const bool valid) const {
  for (ImageMetadata& item : imageMetadata_) {
    if (item.path.compare(path.c_str()) == 0 && item.format == format) {
      item.width = width;
      item.height = height;
      item.valid = valid;
      imageMetadataDirty_ = true;
      return;
    }
  }

  constexpr size_t maxEntries = 256;
  if (imageMetadata_.size() >= maxEntries) imageMetadata_.erase(imageMetadata_.begin());
  imageMetadata_.push_back({EpubPsramString(path.c_str()), width, height, format, valid});
  imageMetadataDirty_ = true;
}

void Epub::invalidateImageMetadata(const std::string& path) const {
  for (auto it = imageMetadata_.begin(); it != imageMetadata_.end();) {
    if (it->path.compare(path.c_str()) == 0) {
      it = imageMetadata_.erase(it);
      imageMetadataDirty_ = true;
    } else {
      ++it;
    }
  }
}

bool Epub::flushImageMetadata() {
  if (!imageMetadataDirty_ || !bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return true;
  }
  std::vector<BookMetadataCache::ImageMetadataEntry> entries;
  entries.reserve(imageMetadata_.size());
  for (const ImageMetadata& item : imageMetadata_) {
    if (!item.valid || item.width <= 0 || item.height <= 0 || item.width > UINT16_MAX || item.height > UINT16_MAX) {
      continue;
    }
    entries.push_back({std::string(item.path.c_str()), static_cast<uint16_t>(item.width),
                       static_cast<uint16_t>(item.height), item.format, item.valid});
  }
  if (!bookMetadataCache->saveImageMetadata(entries)) {
    return false;
  }
  imageMetadataDirty_ = false;
  return true;
}

/**
 * @brief Gets the number of spine items in the book.
 *
 * @return Number of spine items, or 0 if no book is loaded
 */
int Epub::getSpineItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getSpineCount() : 0;
}

/**
 * @brief Retrieves a spine item by index.
 *
 * @param spineIndex Index of the spine item to retrieve
 * @return Spine entry containing the item details
 */
BookMetadataCache::SpineEntry Epub::getSpineItem(int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getSpineEntry(spineIndex);
}

/**
 * @brief Gets the number of TOC items in the book.
 *
 * @return Number of TOC items, or 0 if no book is loaded
 */
int Epub::getTocItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getTocCount() : 0;
}

/**
 * @brief Retrieves a TOC item by index.
 *
 * @param tocIndex Index of the TOC item to retrieve
 * @return TOC entry containing the item details
 */
BookMetadataCache::TocEntry Epub::getTocItem(int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getTocEntry(tocIndex);
}

/**
 * @brief Gets the number of CSS files in the book.
 *
 * @return Number of CSS files, or 0 if no book is loaded
 */
int Epub::getCssItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getCssCount() : 0;
}

/**
 * @brief Retrieves a CSS entry by index.
 *
 * @param cssIndex Index of the CSS entry to retrieve
 * @return CSS entry containing the file details and content
 */
BookMetadataCache::CssEntry Epub::getCssItem(int cssIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getCssEntry(cssIndex);
}

/**
 * @brief Gets CSS content by file path.
 *
 * @param cssPath Internal path to the CSS file
 * @return CSS content as string, empty if not found
 */
std::string Epub::getCssContent(const std::string& cssPath) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return "";
  return bookMetadataCache->getCssContent(cssPath);
}

/**
 * @brief Gets all CSS file paths in the book.
 *
 * @return Vector of CSS file paths
 */
std::vector<std::string> Epub::getAllCssPaths() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getAllCssPaths();
}

/**
 * @brief Gets combined CSS content from all CSS files.
 *
 * @return Combined CSS content as a single string
 */
std::string Epub::getCombinedCss() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return "";

  std::string combined;
  auto paths = getAllCssPaths();

  for (const auto& path : paths) {
    std::string cssContent = getCssContent(path);
    if (!cssContent.empty()) {
      if (!combined.empty()) {
        combined += "\n\n/* === " + path + " === */\n\n";
      }
      combined += cssContent;
    }
  }

  return combined;
}

std::string Epub::parsedCssCachePath() const { return cachePath + "/parsed_css.bin"; }

bool Epub::loadParsedCssCache() const {
  if (!SdMan.exists(parsedCssCachePath().c_str())) {
    return false;
  }
  FsFile file;
  if (!SdMan.openFileForRead("EBP", parsedCssCachePath(), file)) {
    return false;
  }
  parsedCssParser_.reset(new (std::nothrow) CssParser());
  if (!parsedCssParser_) {
    file.close();
    return false;
  }
  const bool ok = parsedCssParser_->loadBinary(file);
  file.close();
  if (!ok) {
    parsedCssParser_.reset();
    SdMan.remove(parsedCssCachePath().c_str());
    INX_SERIAL.printf("[EBP] Removed invalid parsed CSS cache\n");
    return false;
  }
  INX_SERIAL.printf("[EBP] Loaded parsed CSS cache: %zu rules\n", parsedCssParser_->getRuleCount());
  return true;
}

bool Epub::saveParsedCssCache() const {
  if (!parsedCssParser_) {
    return false;
  }
  const std::string tempPath = cachePath + "/parsed_css.bin.tmp";
  FsFile file;
  if (!SdMan.openFileForWrite("EBP", tempPath, file)) {
    return false;
  }
  const bool ok = parsedCssParser_->saveBinary(file);
  file.close();
  if (!ok) {
    SdMan.remove(tempPath.c_str());
    INX_SERIAL.printf("[EBP] Failed to write parsed CSS cache\n");
    return false;
  }
  if (SdMan.exists(parsedCssCachePath().c_str())) {
    SdMan.remove(parsedCssCachePath().c_str());
  }
  const bool renamed = SdMan.rename(tempPath.c_str(), parsedCssCachePath().c_str());
  if (!renamed) {
    SdMan.remove(tempPath.c_str());
  }
  return renamed;
}

const CssParser* Epub::getParsedCssParser(const CssParser::UsageFilter* usageFilter) const {
  (void)usageFilter;
  if (parsedCssLoaded_) {
    return parsedCssParser_.get();
  }
  parsedCssLoaded_ = true;

  if (loadParsedCssCache()) {
    return parsedCssParser_.get();
  }

  constexpr uint32_t kMinFreeHeapForCss = 48 * 1024;
  if (ESP.getFreeHeap() < kMinFreeHeapForCss) {
    INX_SERIAL.printf("[EBP] Low heap (%u bytes), skipping EPUB stylesheet CSS\n",
                  static_cast<unsigned>(ESP.getFreeHeap()));
    return nullptr;
  }

  parsedCssParser_.reset(new (std::nothrow) CssParser());
  if (!parsedCssParser_) {
    INX_SERIAL.printf("[EBP] Failed to allocate parsed CSS dictionary\n");
    return nullptr;
  }

  const int cssCount = getCssItemsCount();
  if (cssCount <= 0) {
    return parsedCssParser_.get();
  }

  INX_SERIAL.printf("[EBP] Building shared CSS dictionary from %d CSS files\n", cssCount);

  constexpr size_t kMaxTotalCssSize = 192 * 1024;
  constexpr uint32_t kCssReserveHeapBytes = 96 * 1024;
  constexpr uint32_t kCssEntryReadHeadroom = 56 * 1024;
  size_t totalCssSize = 0;

  for (int i = 0; i < cssCount && totalCssSize < kMaxTotalCssSize; ++i) {
    if (ESP.getFreeHeap() < kCssReserveHeapBytes + kCssEntryReadHeadroom) {
      INX_SERIAL.printf("[EBP] CSS load stopped to reserve heap before file %d (free=%u, rules=%zu)\n", i,
                    static_cast<unsigned>(ESP.getFreeHeap()), parsedCssParser_->getRuleCount());
      break;
    }
    try {
      const auto cssEntry = getCssItem(i);
      if (cssEntry.content.empty()) {
        continue;
      }
      if (cssEntry.content.size() > 64 * 1024) {
        INX_SERIAL.printf("[EBP] Skipping large CSS file: %s (%d bytes)\n", cssEntry.path.c_str(),
                      static_cast<int>(cssEntry.content.size()));
        continue;
      }
      totalCssSize += cssEntry.content.size();
      parsedCssParser_->parse(cssEntry.content, cssEntry.path, kCssReserveHeapBytes, nullptr);
      if (ESP.getFreeHeap() < kCssReserveHeapBytes + kCssEntryReadHeadroom) {
        INX_SERIAL.printf("[EBP] CSS load stopped to reserve heap after file %d (free=%u, rules=%zu)\n", i,
                      static_cast<unsigned>(ESP.getFreeHeap()), parsedCssParser_->getRuleCount());
        break;
      }
    } catch (const std::exception& e) {
      INX_SERIAL.printf("[EBP] Shared CSS load aborted at file %d (%s); keeping %zu rules\n", i, e.what(),
                    parsedCssParser_->getRuleCount());
      break;
    } catch (...) {
      INX_SERIAL.printf("[EBP] Shared CSS load aborted at file %d; keeping %zu rules\n", i,
                    parsedCssParser_->getRuleCount());
      break;
    }
  }

  INX_SERIAL.printf("[EBP] Shared CSS dictionary: %zu rules from %d bytes\n", parsedCssParser_->getRuleCount(),
                static_cast<int>(totalCssSize));
  if (!saveParsedCssCache()) {
    INX_SERIAL.printf("[EBP] Parsed CSS cache was not saved\n");
  }
  return parsedCssParser_.get();
}

void Epub::releaseParsedCssParser() const {
  parsedCssParser_.reset();
  parsedCssLoaded_ = false;
}

/**
 * @brief Gets the spine index for a given TOC index.
 *
 * @param tocIndex TOC index to look up
 * @return Corresponding spine index, or 0 if not found
 */
int Epub::getSpineIndexForTocIndex(int tocIndex) const { return getTocItem(tocIndex).spineIndex; }

/**
 * @brief Gets the TOC index for a given spine index.
 *
 * @param spineIndex Spine index to look up
 * @return Corresponding TOC index, or 0 if not found
 */
int Epub::getTocIndexForSpineIndex(int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

/**
 * @brief Gets the cumulative size up to a specific spine item.
 *
 * @param spineIndex Spine index to get cumulative size for
 * @return Total size in bytes up to and including the specified spine item
 */
size_t Epub::getCumulativeSpineItemSize(int spineIndex) const { return getSpineItem(spineIndex).cumulativeSize; }

/**
 * @brief Finds the spine index for the text reference href.
 *
 * @return Spine index of the text reference, or 0 if not found
 */
int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return 0;
  const std::string& ref = bookMetadataCache->coreMetadata.textReferenceHref;
  if (ref.empty()) return 0;
  for (int i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == ref) return i;
  }
  return 0;
}

int Epub::getSpineIndexForInitialOpen() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || getSpineItemsCount() <= 0) {
    return 0;
  }

  const std::string& ref = bookMetadataCache->coreMetadata.textReferenceHref;
  if (!ref.empty()) {
    const int tr = getSpineIndexForTextReference();
    if (tr >= 0 && tr < getSpineItemsCount()) {
      const std::string& sh = getSpineItem(tr).href;
      if (spineHrefLooksLikeRenderableHtml(sh)) {
        return tr;
      }
    }
  }

  for (int ti = 0; ti < getTocItemsCount(); ++ti) {
    const int sp = getTocItem(ti).spineIndex;
    if (sp < 0 || sp >= getSpineItemsCount()) {
      continue;
    }
    if (spineHrefLooksLikeRenderableHtml(getSpineItem(sp).href)) {
      return sp;
    }
  }

  for (int i = 0; i < getSpineItemsCount(); ++i) {
    if (spineHrefLooksLikeRenderableHtml(getSpineItem(i).href)) {
      return i;
    }
  }

  return 0;
}

/**
 * @brief Calculates the total size of the book in bytes.
 *
 * @return Total size of all spine items combined
 */
size_t Epub::getBookSize() const {
  int count = getSpineItemsCount();
  return (count > 0) ? getCumulativeSpineItemSize(count - 1) : 0;
}

/**
 * @brief Calculates the reading progress percentage.
 *
 * @param currentSpineIndex Current spine item index
 * @param currentSpineRead Progress within the current spine item (0.0 to 1.0)
 * @return Progress value between 0.0 and 1.0
 */
float Epub::calculateProgress(int currentSpineIndex, float currentSpineRead) const {
  size_t total = getBookSize();
  if (total == 0) return 0.0f;
  size_t prev = (currentSpineIndex > 0) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  size_t current = getCumulativeSpineItemSize(currentSpineIndex) - prev;
  float progressed = static_cast<float>(prev) + (currentSpineRead * current);
  return progressed / static_cast<float>(total);
}
