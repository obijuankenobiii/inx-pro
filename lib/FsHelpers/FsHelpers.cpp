/**
 * @file FsHelpers.cpp
 * @brief Definitions for FsHelpers.
 */

#include "FsHelpers.h"

#include <cctype>
#include <vector>

namespace {
int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}

std::string FsHelpers::decodeUriPath(const std::string& path) {
  std::string decoded;
  decoded.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      const int high = hexValue(path[i + 1]);
      const int low = hexValue(path[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    decoded.push_back(path[i]);
  }
  return decoded;
}

std::string FsHelpers::resolveRelativePath(const std::string& currentFile, const std::string& relativePath) {
  if (relativePath.empty()) return "";

  if (relativePath[0] == '/') return normalisePath(relativePath);

  size_t lastSlash = currentFile.find_last_of('/');
  std::string baseDir = (lastSlash == std::string::npos) ? "" : currentFile.substr(0, lastSlash + 1);

  return normalisePath(baseDir + relativePath);
}

std::string FsHelpers::normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) components.pop_back();
        } else if (component != ".") {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    if (component == "..") {
      if (!components.empty()) components.pop_back();
    } else if (component != ".") {
      components.push_back(component);
    }
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) result += "/";
    result += c;
  }

  return result;
}
