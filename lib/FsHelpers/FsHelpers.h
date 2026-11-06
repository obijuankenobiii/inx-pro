#pragma once

/**
 * @file FsHelpers.h
 * @brief Public interface and types for FsHelpers.
 */

#include <string>

class FsHelpers {
 public:
  /** Decodes percent-escaped bytes in an EPUB/URI path without changing '+' characters. */
  static std::string decodeUriPath(const std::string& path);
  static std::string normalisePath(const std::string& path);
  static std::string resolveRelativePath(const std::string& currentFile, const std::string& relativePath);
};
