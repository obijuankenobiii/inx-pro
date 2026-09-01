/**
 * @file BookState.cpp
 * @brief Definitions for BookState.
 */

#include "state/BookState.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {
constexpr uint8_t BOOKS_FILE_VERSION = 2;
constexpr uint8_t BOOKS_FILE_VERSION_V1 = 1;
constexpr char BOOKS_FILE[] = "/.metadata/books.bin";
constexpr uint32_t MAX_BOOK_STATE_STRING_LENGTH = 4096;

template <typename T>
bool readBookPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool readBookString(FsFile& file, std::string& value) {
  uint32_t length = 0;
  value.clear();
  if (!readBookPod(file, length)) {
    return false;
  }

  const int available = file.available();
  if (length > MAX_BOOK_STATE_STRING_LENGTH || available < 0 ||
      static_cast<uint64_t>(length) > static_cast<uint64_t>(available)) {
    return false;
  }

  if (length == 0) {
    return true;
  }

  value.resize(length);
  if (file.read(reinterpret_cast<uint8_t*>(&value[0]), length) != length) {
    value.clear();
    return false;
  }
  return true;
}

/** Reads every record from BOOKS_FILE into a fresh local vector - the only representation of "all
 *  books" that ever exists. Callers use it and let it fall out of scope; nothing is cached between
 *  calls. Returns the file's stored next-id counter (1 if the file is missing/unreadable). */
uint32_t loadAllBooks(std::vector<BookState::Book>& out) {
  out.clear();

  FsFile inputFile;
  if (!SdMan.openFileForRead("BKS", BOOKS_FILE, inputFile)) {
    return 1;
  }

  uint8_t version = 0;
  if (!readBookPod(inputFile, version)) {
    inputFile.close();
    return 1;
  }
  if (version != BOOKS_FILE_VERSION && version != BOOKS_FILE_VERSION_V1) {
    inputFile.close();
    return 1;
  }

  uint32_t nextId = 1;
  if (!readBookPod(inputFile, nextId)) {
    inputFile.close();
    return 1;
  }

  size_t count = 0;
  if (version == BOOKS_FILE_VERSION_V1) {
    uint16_t count16 = 0;
    if (!readBookPod(inputFile, count16)) {
      inputFile.close();
      return 1;
    }
    count = count16;
  } else {
    uint32_t count32 = 0;
    if (!readBookPod(inputFile, count32)) {
      inputFile.close();
      return 1;
    }
    count = static_cast<size_t>(count32);
  }

  constexpr size_t kMinimumBookRecordBytes = sizeof(uint32_t) * 3 + sizeof(uint32_t) + sizeof(uint8_t);
  const int remaining = inputFile.available();
  if (remaining < 0 || count > static_cast<size_t>(remaining) / kMinimumBookRecordBytes) {
    INX_SERIAL.printf("[%lu] [BOOK_STATE] invalid record count=%u remaining=%d\n", millis(),
                  static_cast<unsigned>(count), remaining);
    inputFile.close();
    return 1;
  }

  out.reserve(count);
  for (size_t i = 0; i < count; i++) {
    BookState::Book book;
    if (!readBookString(inputFile, book.path) || !readBookString(inputFile, book.title) ||
        !readBookString(inputFile, book.author) || !readBookPod(inputFile, book.id)) {
      INX_SERIAL.printf("[%lu] [BOOK_STATE] truncated record=%u\n", millis(), static_cast<unsigned>(i));
      out.clear();
      inputFile.close();
      return 1;
    }

    uint8_t flags = 0;
    if (!readBookPod(inputFile, flags)) {
      INX_SERIAL.printf("[%lu] [BOOK_STATE] truncated flags record=%u\n", millis(), static_cast<unsigned>(i));
      out.clear();
      inputFile.close();
      return 1;
    }
    book.isFavorite = (flags & 0x01) != 0;
    book.isReading = (flags & 0x02) != 0;
    book.isFinished = (flags & 0x04) != 0;

    out.push_back(std::move(book));
    if ((i % 256u) == 255u) {
      yield();
    }
  }

  inputFile.close();
  return nextId;
}

/** Drops title (idle books) and author (all books) before writing, same as the old in-RAM
 *  compactIdleMetadata() pass - keeps the file itself, and thus every future load, small. Display
 *  surfaces for favorite/reading books (the only ones that keep a title) already have their own
 *  copies of title/author (RecentBooks, book metadata cache) to fall back on. */
void compactForWrite(std::vector<BookState::Book>& books) {
  for (auto& book : books) {
    if (!book.isFavorite && !book.isReading) {
      std::string().swap(book.title);
    }
    std::string().swap(book.author);
  }
}

bool writeAllBooks(const std::vector<BookState::Book>& books, const uint32_t nextId) {
  SdMan.mkdir("/.metadata");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("BKS", BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, BOOKS_FILE_VERSION);
  serialization::writePod(outputFile, nextId);
  serialization::writePod(outputFile, static_cast<uint32_t>(books.size()));

  uint32_t written = 0;
  for (const auto& book : books) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writePod(outputFile, book.id);

    uint8_t flags = 0;
    if (book.isFavorite) flags |= 0x01;
    if (book.isReading) flags |= 0x02;
    if (book.isFinished) flags |= 0x04;
    serialization::writePod(outputFile, flags);
    if ((++written % 256u) == 0u) {
      yield();
    }
  }

  outputFile.close();
  return true;
}

std::vector<BookState::Book> filterAndSort(std::vector<BookState::Book>& books, bool (*pred)(const BookState::Book&)) {
  std::vector<BookState::Book> result;
  for (auto& b : books) {
    if (pred(b)) {
      result.push_back(std::move(b));
    }
  }
  std::sort(result.begin(), result.end(),
           [](const BookState::Book& a, const BookState::Book& b) { return a.id > b.id; });
  return result;
}
}

BookState BookState::instance;

bool BookState::findBook(const std::string& path, Book& out) const {
  std::vector<Book> books;
  loadAllBooks(books);
  for (auto& b : books) {
    if (b.path == path) {
      out = std::move(b);
      return true;
    }
  }
  return false;
}

void BookState::addOrUpdateBook(const std::string& path, const std::string& title, const std::string& author) {
  std::vector<Book> books;
  uint32_t nextId = loadAllBooks(books);

  bool found = false;
  for (auto& b : books) {
    if (b.path == path) {
      b.title = title;
      b.author = author;
      found = true;
      break;
    }
  }
  if (!found) {
    books.push_back(Book(path, title, author, nextId++));
  }

  compactForWrite(books);
  writeAllBooks(books, nextId);
}

void BookState::toggleFavorite(const std::string& path, const std::string& fallbackTitle) {
  std::vector<Book> books;
  uint32_t nextId = loadAllBooks(books);

  Book* target = nullptr;
  for (auto& b : books) {
    if (b.path == path) {
      target = &b;
      break;
    }
  }
  if (target == nullptr) {
    books.push_back(Book(path, fallbackTitle, "", nextId++));
    target = &books.back();
  }

  target->isFavorite = !target->isFavorite;
  if (target->isFavorite && target->title.empty()) {
    target->title = fallbackTitle;
  }

  compactForWrite(books);
  writeAllBooks(books, nextId);
}

void BookState::setReading(const std::string& path, const bool isReading, const std::string& fallbackTitle) {
  std::vector<Book> books;
  uint32_t nextId = loadAllBooks(books);

  Book* target = nullptr;
  for (auto& b : books) {
    if (b.path == path) {
      target = &b;
      break;
    }
  }
  if (target == nullptr) {
    if (!isReading) {
      return;
    }
    books.push_back(Book(path, fallbackTitle, "", nextId++));
    target = &books.back();
  }

  target->isReading = isReading;
  if (isReading && target->title.empty()) {
    target->title = fallbackTitle;
  }

  compactForWrite(books);
  writeAllBooks(books, nextId);
}

void BookState::setFinished(const std::string& path, const bool finished) {
  std::vector<Book> books;
  const uint32_t nextId = loadAllBooks(books);

  for (auto& b : books) {
    if (b.path == path) {
      b.isFinished = finished;
      if (finished) b.isReading = false;
      compactForWrite(books);
      writeAllBooks(books, nextId);
      return;
    }
  }
}

void BookState::removeBook(const std::string& path) {
  std::vector<Book> books;
  const uint32_t nextId = loadAllBooks(books);

  const auto it = std::find_if(books.begin(), books.end(), [&](const Book& b) { return b.path == path; });
  if (it == books.end()) {
    return;
  }
  books.erase(it);
  writeAllBooks(books, nextId);
}

void BookState::renamePath(const std::string& oldPath, const std::string& newPath) {
  std::vector<Book> books;
  const uint32_t nextId = loadAllBooks(books);

  for (auto& b : books) {
    if (b.path == oldPath) {
      b.path = newPath;
      writeAllBooks(books, nextId);
      return;
    }
  }
}

void BookState::clear(const bool writeFile) {
  if (!writeFile) {
    return;
  }
  writeAllBooks({}, 1);
}

std::vector<BookState::Book> BookState::getAllBooks() const {
  std::vector<Book> books;
  loadAllBooks(books);
  return books;
}

std::vector<BookState::Book> BookState::getFavoriteBooks() const {
  std::vector<Book> books;
  loadAllBooks(books);
  return filterAndSort(books, [](const Book& b) { return b.isFavorite; });
}

std::vector<BookState::Book> BookState::getReadingBooks() const {
  std::vector<Book> books;
  loadAllBooks(books);
  return filterAndSort(books, [](const Book& b) { return b.isReading; });
}

std::vector<BookState::Book> BookState::getFinishedBooks() const {
  std::vector<Book> books;
  loadAllBooks(books);
  return filterAndSort(books, [](const Book& b) { return b.isFinished; });
}
