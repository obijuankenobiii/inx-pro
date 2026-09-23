#pragma once

/**
 * @file ContentOpfParser.h
 * @brief Public interface and types for ContentOpfParser.
 */

#include <Print.h>

#include <vector>

#include "Epub.h"
#include "expat.h"

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_DESCRIPTION,
    IN_BOOK_LANGUAGE,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
  };

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  FsFile tempItemStore;
  std::string coverItemId;
  int metadataElementDepth = -1;
  int activeMetadataField = -1;
  size_t capturedMetadataBytes = 0;
  std::vector<BookMetadataCache::MetadataField> metadataFields;
  std::vector<std::string> tags;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::string title;
  std::string author;
  std::string description;
  std::string language;
  std::string tocNcxPath;
  std::string tocNavPath;
  std::string coverItemHref;
  std::string textReferenceHref;
  std::string titleSort;
  std::string authorSort;
  std::string series;
  std::string seriesIndex;
  std::string rating;
  std::string publisher;
  std::string publicationDate;
  std::string calibreTimestamp;
  std::string calibreUuid;

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache)
      : cachePath(cachePath), baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~ContentOpfParser() override;

  bool setup();
  BookMetadataCache::BookMetadata takeMetadata();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  struct ManifestItem {
    std::string href;
    std::string mimeType;
  };
  std::vector<ManifestItem> getImages() const;
};
