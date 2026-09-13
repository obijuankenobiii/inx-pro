#pragma once

#include <cstdint>
#include <functional>
#include <string>

class Epub;

/** Optional Study Cards plugin storage and export API. */
class StudyCards {
 public:
  struct Card {
    std::string id;
    std::string front;
    std::string back;
    std::string context;
    std::string book;
    std::string chapter;
    std::string tags;
    uint32_t page = 0;
    uint32_t spine = 0;
    uint32_t created = 0;
  };

  using CardCallback = std::function<bool(const Card&)>;

  static bool isInstalled();
  static bool add(Epub& epub, const std::string& chapter, uint16_t spine, uint16_t page,
                  const std::string& selectedText, const std::string& context = {});
  static bool forEach(const CardCallback& callback);
  static bool exportJson(const std::function<bool(const std::string&)>& writer);
  static bool exportAnki(const std::function<bool(const std::string&)>& writer);

 private:
  StudyCards() = delete;
};
