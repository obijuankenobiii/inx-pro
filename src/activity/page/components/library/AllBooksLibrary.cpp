#include "AllBooksLibrary.h"

#include <algorithm>

bool AllBooksLibrary::load(std::vector<LibraryIndex::Book>& books) {
  if (!LibraryIndex::search("", books, LibraryIndex::all)) return false;

  books.erase(std::remove_if(books.begin(), books.end(), [](const LibraryIndex::Book& book) {
                return book.type != LibraryIndex::Book::Type::BOOK;
              }),
              books.end());
  return true;
}
