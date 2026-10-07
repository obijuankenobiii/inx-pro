#pragma once

#include <string>

class UriGlob {
 public:
  explicit UriGlob(const char *pattern) : pattern_(pattern ? pattern : "") {}
  operator const char *() const { return pattern_.c_str(); }

 private:
  std::string pattern_;
};
