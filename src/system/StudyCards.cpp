#include "StudyCards.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Epub.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>

#include "PluginManager.h"
#include "util/SdIoMutex.h"

namespace {
constexpr char kPluginId[] = "study-cards";
constexpr char kDataDir[] = "/.system/plugins/study-cards";
constexpr char kCardsPath[] = "/.system/plugins/study-cards/cards.jsonl";
constexpr size_t kMaxCardText = 4096;

std::string clipped(const std::string& value) {
  return value.size() <= kMaxCardText ? value : value.substr(0, kMaxCardText);
}

std::string htmlEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string ankiField(const std::string& value) {
  std::string out = htmlEscape(value);
  std::string result;
  result.reserve(out.size());
  for (const char c : out) {
    if (c == '\t') {
      result += " ";
    } else if (c == '\r' || c == '\n') {
      if (result.size() < 5 || result.compare(result.size() - 5, 5, "<br>") != 0) result += "<br>";
    } else {
      result += c;
    }
  }
  return result;
}

bool parseCard(const String& line, StudyCards::Card& card) {
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok) return false;
  card.id = doc["id"] | "";
  card.front = doc["front"] | "";
  card.back = doc["back"] | "";
  card.context = doc["context"] | "";
  card.book = doc["book"] | "";
  card.chapter = doc["chapter"] | "";
  card.tags = doc["tags"] | "";
  card.page = doc["page"] | 0;
  card.spine = doc["spine"] | 0;
  card.created = doc["created"] | 0;
  return !card.id.empty() && !card.front.empty();
}

uint32_t nextId() {
  static uint32_t sequence = 0;
  return ++sequence + millis();
}
}  // namespace

bool StudyCards::isInstalled() { return PluginManager::isInstalled(kPluginId); }

bool StudyCards::add(Epub& epub, const std::string& chapter, const uint16_t spine, const uint16_t page,
                     const std::string& selectedText, const std::string& context) {
  if (!isInstalled() || selectedText.empty() || !SdMan.ready()) return false;

  SdIoMutex::Lock ioLock;
  SdMan.mkdir("/.system");
  SdMan.mkdir("/.system/plugins");
  SdMan.mkdir(kDataDir);
  FsFile file = SdMan.open(kCardsPath, O_WRITE | O_CREAT | O_APPEND);
  if (!file) return false;

  JsonDocument doc;
  const uint32_t created = millis() / 1000;
  const uint32_t idValue = nextId();
  char id[24];
  snprintf(id, sizeof(id), "%08lx-%08lx", static_cast<unsigned long>(created),
           static_cast<unsigned long>(idValue));
  doc["id"] = id;
  doc["front"] = clipped(selectedText);
  doc["back"] = "";
  doc["context"] = clipped(context.empty() ? selectedText : context);
  doc["book"] = epub.getTitle();
  doc["chapter"] = chapter;
  doc["tags"] = "inx study";
  doc["page"] = page;
  doc["spine"] = spine;
  doc["created"] = created;

  const size_t written = serializeJson(doc, file);
  file.write(static_cast<uint8_t>('\n'));
  file.close();
  return written > 0;
}

bool StudyCards::forEach(const CardCallback& callback) {
  if (!isInstalled() || !callback || !SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  FsFile file = SdMan.open(kCardsPath, O_READ);
  if (!file) return true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    Card card;
    if (parseCard(line, card) && !callback(card)) {
      file.close();
      return false;
    }
    yield();
  }
  file.close();
  return true;
}

bool StudyCards::exportJson(const std::function<bool(const std::string&)>& writer) {
  if (!writer || !isInstalled()) return false;
  if (!writer("[")) return false;
  bool first = true;
  const bool ok = forEach([&](const Card& card) {
    JsonDocument doc;
    doc["id"] = card.id;
    doc["front"] = card.front;
    doc["back"] = card.back;
    doc["context"] = card.context;
    doc["book"] = card.book;
    doc["chapter"] = card.chapter;
    doc["tags"] = card.tags;
    doc["page"] = card.page;
    doc["spine"] = card.spine;
    doc["created"] = card.created;
    String json;
    serializeJson(doc, json);
    if (!first && !writer(",")) return false;
    first = false;
    return writer(json.c_str());
  });
  return ok && writer("]");
}

bool StudyCards::exportAnki(const std::function<bool(const std::string&)>& writer) {
  if (!writer || !isInstalled()) return false;
  if (!writer("#separator:Tab\n#html:true\n#tags column:3\n")) return false;
  return forEach([&](const Card& card) {
    std::string line = ankiField(card.front) + "\t" + ankiField(card.back) + "\t" + ankiField(card.tags) + "\n";
    return writer(line);
  });
}
