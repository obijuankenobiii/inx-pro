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

  // Files larger than this are rejected up front (Phase 1 holds the whole file in RAM/PSRAM for simple,
  // pointer-based parsing rather than streaming - a real streaming xref/object reader is future work).
  static constexpr size_t kMaxSupportedFileSize = 6u * 1024u * 1024u;

  bool open(const std::string& path);
  void close();
  bool isOpen() const { return loaded_; }
  OpenError lastError() const { return lastError_; }

  int getPageCount() const { return static_cast<int>(pages_.size()); }

  // Fills outPageDict with the page's own entries plus inherited /Resources, /MediaBox, /Rotate merged in
  // (synthesized keys, since PDF lets those live on ancestor Pages nodes). Returns false if index is out of range.
  bool getPage(int index, PdfObject& outPageDict) const;

  // Follows a Reference to the parsed object it points to; returns the object unchanged otherwise.
  PdfObject resolve(const PdfObject& obj) const;

  // Parses (and caches) the indirect object with the given number, regardless of generation (Phase 1 doesn't
  // track multiple generations - the classic xref table only ever has one live entry per object number anyway).
  const PdfObject& getObject(uint32_t num) const;

  // Decodes a stream object's bytes through its /Filter chain (FlateDecode, ASCIIHexDecode, ASCII85Decode).
  // Returns false if the object isn't a stream or a filter is unsupported.
  bool getStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const;

  // Returns the raw bytes of a stream before filter decoding. Used for embedded JPEG image XObjects.
  bool getRawStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const;

  std::string getTitleFromInfo() const;

 private:
  struct PageEntry {
    PdfObject ref;             // Reference to the Page dict
    PdfObject inheritedAttrs;  // synthesized Dictionary of /Resources, /MediaBox, /Rotate inherited from ancestors
  };

  struct XrefEntry {
    uint8_t type = 0;  // 0 free, 1 uncompressed, 2 compressed in an object stream
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
