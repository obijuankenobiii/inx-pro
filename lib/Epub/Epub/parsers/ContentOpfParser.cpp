/**
 * @file ContentOpfParser.cpp
 * @brief Definitions for ContentOpfParser.
 */

#include "ContentOpfParser.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "../BookMetadataCache.h"

namespace {
constexpr char MEDIA_TYPE_NCX[] = "application/x-dtbncx+xml";
constexpr char itemCacheFile[] = "/.items.bin";
constexpr size_t kMaxDescriptionLength = 1024;
constexpr size_t kMaxMetadataFields = 128;
constexpr size_t kMaxMetadataAttributes = 16;
constexpr size_t kMaxCapturedMetadataBytes = 64 * 1024;
constexpr size_t kMaxMetadataValueLength = 16 * 1024;
constexpr size_t kMaxMetadataTags = 256;

std::string localName(const std::string& name) {
  const size_t colon = name.find_last_of(':');
  std::string result = colon == std::string::npos ? name : name.substr(colon + 1);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string normalizedMetadataText(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  bool pendingSpace = false;
  for (const unsigned char c : value) {
    if (std::isspace(c)) {
      pendingSpace = !result.empty();
      continue;
    }
    if (pendingSpace) {
      result.push_back(' ');
      pendingSpace = false;
    }
    result.push_back(static_cast<char>(c));
  }
  return result;
}

const std::string* metadataAttribute(const BookMetadataCache::MetadataField& field, const char* name) {
  for (const auto& attribute : field.attributes) {
    if (localName(attribute.name) == name) return &attribute.value;
  }
  return nullptr;
}

void appendAuthorSort(std::string& destination, const std::string& sort) {
  const std::string normalized = normalizedMetadataText(sort);
  if (normalized.empty()) return;
  if (!destination.empty()) destination += "; ";
  destination += normalized;
}
}

bool ContentOpfParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    INX_SERIAL.printf("[%lu] [COF] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

ContentOpfParser::~ContentOpfParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);
    XML_SetElementHandler(parser, nullptr, nullptr);
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
  if (tempItemStore) {
    tempItemStore.close();
  }
  if (SdMan.exists((cachePath + itemCacheFile).c_str())) {
    SdMan.remove((cachePath + itemCacheFile).c_str());
  }
}

size_t ContentOpfParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContentOpfParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);

    if (!buf) {
      INX_SERIAL.printf("[%lu] [COF] Couldn't allocate memory for buffer\n", millis());
      XML_StopParser(parser, XML_FALSE);
      XML_SetElementHandler(parser, nullptr, nullptr);
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      INX_SERIAL.printf("[%lu] [COF] Parse error at line %lu: %s\n", millis(), XML_GetCurrentLineNumber(parser),
                    XML_ErrorString(XML_GetErrorCode(parser)));
      XML_StopParser(parser, XML_FALSE);
      XML_SetElementHandler(parser, nullptr, nullptr);
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }

  return size;
}

void XMLCALL ContentOpfParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->metadataElementDepth >= 0) {
    if (self->metadataElementDepth == 0 && self->metadataFields.size() < kMaxMetadataFields) {
      BookMetadataCache::MetadataField field;
      if (std::strlen(name) <= 128) field.name = name;
      for (int i = 0; atts && atts[i] && atts[i + 1] && field.attributes.size() < kMaxMetadataAttributes; i += 2) {
        const size_t nameLength = std::strlen(atts[i]);
        const size_t valueLength = std::strlen(atts[i + 1]);
        if (nameLength > 128 || valueLength > kMaxMetadataValueLength ||
            nameLength + valueLength > kMaxCapturedMetadataBytes - self->capturedMetadataBytes) {
          continue;
        }
        field.attributes.push_back({atts[i], atts[i + 1]});
        self->capturedMetadataBytes += nameLength + valueLength;
      }
      self->metadataFields.push_back(std::move(field));
      self->activeMetadataField = static_cast<int>(self->metadataFields.size() - 1);
    } else if (self->metadataElementDepth == 0) {
      self->activeMetadataField = -1;
    }
    ++self->metadataElementDepth;
  }

  if (self->state == START && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    self->state = IN_METADATA;
    self->metadataElementDepth = 0;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:title") == 0) {
    self->state = IN_BOOK_TITLE;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:creator") == 0) {
    self->state = IN_BOOK_AUTHOR;
    return;
  }

  if (self->state == IN_METADATA &&
      (strcmp(name, "dc:description") == 0 || strcmp(name, "description") == 0 ||
       strcmp(name, "opf:description") == 0)) {
    self->state = IN_BOOK_DESCRIPTION;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:language") == 0) {
    self->state = IN_BOOK_LANGUAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_MANIFEST;
    if (!SdMan.openFileForWrite("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      INX_SERIAL.printf(
          "[%lu] [COF] Couldn't open temp items file for writing. This is probably going to be a fatal error.\n",
          millis());
    }
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_SPINE;
    if (!SdMan.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      INX_SERIAL.printf(
          "[%lu] [COF] Couldn't open temp items file for reading. This is probably going to be a fatal error.\n",
          millis());
    }

    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_GUIDE;

    INX_SERIAL.printf("[%lu] [COF] Entering guide state.\n", millis());
    if (!SdMan.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      INX_SERIAL.printf(
          "[%lu] [COF] Couldn't open temp items file for reading. This is probably going to be a fatal error.\n",
          millis());
    }
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "meta") == 0 || strcmp(name, "opf:meta") == 0)) {
    bool isCover = false;
    std::string coverItemId;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0 && strcmp(atts[i + 1], "cover") == 0) {
        isCover = true;
      } else if (strcmp(atts[i], "content") == 0) {
        coverItemId = atts[i + 1];
      }
    }

    if (isCover) {
      self->coverItemId = coverItemId;
    }
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "item") == 0 || strcmp(name, "opf:item") == 0)) {
    std::string itemId;
    std::string href;
    std::string mediaType;
    std::string properties;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        itemId = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        href = FsHelpers::normalisePath(self->baseContentPath + atts[i + 1]);
      } else if (strcmp(atts[i], "media-type") == 0) {
        mediaType = atts[i + 1];
      } else if (strcmp(atts[i], "properties") == 0) {
        properties = atts[i + 1];
      }
    }

    serialization::writeString(self->tempItemStore, itemId);
    serialization::writeString(self->tempItemStore, href);

    if (itemId == self->coverItemId) {
      self->coverItemHref = href;
    }

    if (mediaType == MEDIA_TYPE_NCX) {
      if (self->tocNcxPath.empty()) {
        self->tocNcxPath = href;
      } else {
        INX_SERIAL.printf("[%lu] [COF] Warning: Multiple NCX files found in manifest. Ignoring duplicate: %s\n", millis(),
                      href.c_str());
      }
    }

    if (!properties.empty() && self->tocNavPath.empty()) {
      if (properties == "nav" || properties.find("nav ") == 0 || properties.find(" nav") != std::string::npos) {
        self->tocNavPath = href;
        INX_SERIAL.printf("[%lu] [COF] Found EPUB 3 nav document: %s\n", millis(), href.c_str());
      }
    }
    return;
  }

  if (self->cache) {
    if (self->state == IN_SPINE && (strcmp(name, "itemref") == 0 || strcmp(name, "opf:itemref") == 0)) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "idref") == 0) {
          const std::string idref = atts[i + 1];
          std::string href;
          bool found = false;

          self->tempItemStore.seek(0);
          std::string itemId;
          while (self->tempItemStore.available()) {
            serialization::readString(self->tempItemStore, itemId);
            serialization::readString(self->tempItemStore, href);
            if (itemId == idref) {
              found = true;
              break;
            }
          }

          if (found && self->cache) {
            self->cache->createSpineEntry(href);
          }
        }
      }
      return;
    }
  }

  if (self->state == IN_GUIDE && (strcmp(name, "reference") == 0 || strcmp(name, "opf:reference") == 0)) {
    std::string type;
    std::string textHref;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "type") == 0) {
        type = atts[i + 1];
        if (type == "text" || type == "start") {
          continue;
        } else {
          INX_SERIAL.printf("[%lu] [COF] Skipping non-text reference in guide: %s\n", millis(), type.c_str());
          break;
        }
      } else if (strcmp(atts[i], "href") == 0) {
        textHref = FsHelpers::normalisePath(self->baseContentPath + atts[i + 1]);
      }
    }
    if ((type == "text" || (type == "start" && !self->textReferenceHref.empty())) && (textHref.length() > 0)) {
      INX_SERIAL.printf("[%lu] [COF] Found %s reference in guide: %s.\n", millis(), type.c_str(), textHref.c_str());
      self->textReferenceHref = textHref;
    }
    return;
  }
}

void XMLCALL ContentOpfParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->metadataElementDepth > 0 && self->activeMetadataField >= 0 && len > 0 &&
      self->capturedMetadataBytes < kMaxCapturedMetadataBytes) {
    auto& value = self->metadataFields[static_cast<size_t>(self->activeMetadataField)].value;
    const size_t available = std::min(kMaxCapturedMetadataBytes - self->capturedMetadataBytes,
                                      kMaxMetadataValueLength - std::min(value.size(), kMaxMetadataValueLength));
    const size_t toAppend = std::min(static_cast<size_t>(len), available);
    if (toAppend > 0) {
      value.append(s, toAppend);
      self->capturedMetadataBytes += toAppend;
    }
  }

  if (self->state == IN_BOOK_TITLE) {
    self->title.append(s, len);
    return;
  }

  if (self->state == IN_BOOK_AUTHOR) {
    self->author.append(s, len);
    return;
  }

  if (self->state == IN_BOOK_DESCRIPTION && self->description.size() < kMaxDescriptionLength) {
    const size_t remaining = kMaxDescriptionLength - self->description.size();
    self->description.append(s, std::min(static_cast<size_t>(len), remaining));
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE) {
    self->language.append(s, len);
    return;
  }
}

void XMLCALL ContentOpfParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->metadataElementDepth > 0) {
    if (self->metadataElementDepth == 1 && self->activeMetadataField >= 0) {
      auto& field = self->metadataFields[static_cast<size_t>(self->activeMetadataField)];
      if (localName(field.name) == "meta") {
        const std::string* content = metadataAttribute(field, "content");
        if (content) field.value = *content;
      }

      const std::string value = normalizedMetadataText(field.value);
      if (localName(field.name) == "creator") {
        if (const std::string* fileAs = metadataAttribute(field, "file-as")) {
          appendAuthorSort(self->authorSort, *fileAs);
        }
      } else if (localName(field.name) == "subject") {
        if (!value.empty() && self->tags.size() < kMaxMetadataTags) self->tags.push_back(value);
      } else if (localName(field.name) == "publisher") {
        if (self->publisher.empty()) self->publisher = value;
      } else if (localName(field.name) == "date") {
        if (self->publicationDate.empty()) self->publicationDate = value;
      }

      if (localName(field.name) == "meta") {
        const std::string* nameAttr = metadataAttribute(field, "name");
        const std::string* propertyAttr = metadataAttribute(field, "property");
        const std::string metaName = nameAttr ? localName(*nameAttr) : "";
        const std::string metaProperty = propertyAttr ? localName(*propertyAttr) : "";
        if (metaName == "title_sort") self->titleSort = value;
        else if (metaName == "author_sort") self->authorSort = value;
        else if (metaName == "series") self->series = value;
        else if (metaName == "series_index") self->seriesIndex = value;
        else if (metaName == "rating") self->rating = value;
        else if (metaName == "timestamp") self->calibreTimestamp = value;
        else if (metaName == "uuid") self->calibreUuid = value;
        else if (metaProperty == "belongs-to-collection") self->series = value;
        else if (metaProperty == "group-position") self->seriesIndex = value;
      }
      self->activeMetadataField = -1;
    }
    --self->metadataElementDepth;
  } else if (self->metadataElementDepth == 0 && localName(name) == "metadata") {
    self->metadataElementDepth = -1;
  }

  if (self->state == IN_SPINE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_GUIDE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_BOOK_TITLE && strcmp(name, "dc:title") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR && strcmp(name, "dc:creator") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_DESCRIPTION &&
      (strcmp(name, "dc:description") == 0 || strcmp(name, "description") == 0 ||
       strcmp(name, "opf:description") == 0)) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE && strcmp(name, "dc:language") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = START;
    return;
  }
}

BookMetadataCache::BookMetadata ContentOpfParser::takeMetadata() {
  BookMetadataCache::BookMetadata metadata;
  metadata.title = std::move(title);
  metadata.author = std::move(author);
  metadata.description = std::move(description);
  metadata.language = std::move(language);
  metadata.coverItemHref = std::move(coverItemHref);
  metadata.textReferenceHref = std::move(textReferenceHref);
  metadata.titleSort = std::move(titleSort);
  metadata.authorSort = std::move(authorSort);
  metadata.series = std::move(series);
  metadata.seriesIndex = std::move(seriesIndex);
  metadata.rating = std::move(rating);
  metadata.publisher = std::move(publisher);
  metadata.publicationDate = std::move(publicationDate);
  metadata.calibreTimestamp = std::move(calibreTimestamp);
  metadata.calibreUuid = std::move(calibreUuid);
  metadata.tags = std::move(tags);
  metadata.fields = std::move(metadataFields);
  return metadata;
}

std::vector<ContentOpfParser::ManifestItem> ContentOpfParser::getImages() const {
  std::vector<ManifestItem> images;
  FsFile file;

  std::string path = cachePath + "/.items.bin";

  if (!SdMan.openFileForRead("EBP", path, file)) return images;

  while (file.available()) {
    uint8_t idLen = file.read();
    file.seekCur(idLen);

    uint8_t hrefLen = file.read();
    char hrefBuf[hrefLen + 1];
    file.read(hrefBuf, hrefLen);
    hrefBuf[hrefLen] = '\0';

    uint8_t mimeLen = file.read();
    char mimeBuf[mimeLen + 1];
    file.read(mimeBuf, mimeLen);
    mimeBuf[mimeLen] = '\0';

    std::string mime = mimeBuf;
    if (mime.find("image/") == 0) {
      images.push_back({hrefBuf, mime});
    }
  }
  file.close();
  return images;
}
