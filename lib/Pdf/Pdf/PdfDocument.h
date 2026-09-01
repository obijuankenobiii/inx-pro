/**
 * @file PdfDocument.h
 * @brief Loads a PDF file's cross-reference table/stream, resolves indirect objects, and decodes stream filter
 * chains. The whole file is read into RAM/PSRAM once (see kMaxSupportedFileSize).
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "PdfObject.h"

class PdfDocument {
 public:
  enum class OpenError {
    None,
    FileNotFound,
    TooLarge,
    NotAPdf,
    NoStartxref,
    UnsupportedXrefStream,
    NoTrailer,
    NoRootCatalog,
    NoPages,
  };

  static constexpr size_t kMaxSupportedFileSize = 6u * 1024u * 1024u;

  bool open(const std::string& path);
  void close();
  bool isOpen() const { return loaded_; }
  OpenError lastError() const { return lastError_; }

  int getPageCount() const { return static_cast<int>(pages_.size()); }

  bool getPage(int index, PdfObject& outPageDict) const;

  PdfObject resolve(const PdfObject& obj) const;

  const PdfObject& getObject(uint32_t num) const;

  bool getStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const;

  bool getRawStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const;

  std::string getTitleFromInfo() const;

 private:
  struct PageEntry {
    PdfObject ref;
    PdfObject inheritedAttrs;
  };

  struct XrefEntry {
    uint8_t type = 0;
    uint64_t offset = 0;
    uint32_t objectStream = 0;
    uint32_t objectStreamIndex = 0;
  };

  std::vector<uint8_t> fileData_;
  bool loaded_ = false;
  OpenError lastError_ = OpenError::None;
  PdfObject trailer_;
  std::map<uint32_t, XrefEntry> xrefEntries_;
  mutable std::map<uint32_t, PdfObject> objectCache_;
  std::vector<PageEntry> pages_;

  bool parseXrefChain();
  bool parseXrefSectionAt(uint64_t offset, std::vector<uint64_t>& visitedOffsets, PdfObject& outSectionTrailer);
  bool parseXrefStreamAt(uint64_t offset, PdfObject& outSectionTrailer);
  PdfObject parseIndirectObjectAt(uint64_t offset) const;
  PdfObject parseCompressedObject(const XrefEntry& entry) const;
  void collectPages(const PdfObject& nodeRef, const PdfObject& inheritedAttrs, std::vector<uint32_t>& visitedNodes,
                     int depth);
};
