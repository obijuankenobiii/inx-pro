#include "LanguageManager.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "state/SystemSetting.h"
#include "util/SdIoMutex.h"

namespace {

struct Translation {
  std::string key;
  std::string value;
};

std::vector<Translation> translations;
std::string activeCode;
std::string activeName;
std::string bookCode;
bool initialized = false;

struct FallbackKey {
  const char* text;
  const char* key;
};

// This catalog lets existing renderer call sites become language-aware without
// making book titles, authors, or other arbitrary text eligible for translation.
constexpr FallbackKey kFallbackKeys[] = {
    {"Home", "nav.home"},
    {"Library", "nav.library"},
    {"Background", "widget.background"},
    {"Label", "widget.label"},
    {"No books", "widget.no_books"},
    {"Search", "nav.search"},
    {"Settings", "nav.settings"},
    {"Sync", "nav.sync"},
    {"Statistics", "nav.statistics"},
    {"Recent", "nav.recent"},
    {"Reader", "nav.reader"},
    {"System", "nav.system"},
    {"Device", "nav.device"},
    {"Device Management", "nav.device_management"},
    {"Language", "nav.language"},
    {"Presets", "nav.presets"},
    {"Font Manager", "menu.font_manager"},
    {"Language Manager", "menu.language_manager"},
    {"Manage via wifi", "device.manage_via_wifi"},
    {"Calibre File Transfer", "device.calibre_file_transfer"},
    {"Create hotspot", "device.create_hotspot"},
    {"OPDS Browser", "device.opds_browser"},
    {"Backup and restore", "device.backup_restore"},
    {"KOReader Sync", "device.koreader_sync"},
    {"Check for updates", "device.check_updates"},
    {"Choose dictionary", "device.choose_dictionary"},
    {"Device Information", "device.information"},
    {"Shortcuts", "home.shortcuts"},
    {"Bookmarks", "home.bookmarks"},
    {"Highlights", "home.highlights"},
    {"Favorites", "home.favorites"},
    {"Dictionary", "home.dictionary"},
    {"Heatmap", "home.heatmap"},
    {"Favorite", "home.favorite"},
    {"Book", "home.book"},
    {"View Report", "home.view_report"},
    {"Remove favorite", "home.remove_favorite"},
    {"Remove Recent", "home.remove_recent"},
    {"Delete cache", "home.delete_cache"},
    {"No bookmarks yet", "home.no_bookmarks"},
    {"No highlights yet", "home.no_highlights"},
    {"No favorites yet", "home.no_favorites"},
    {"No saved words yet", "home.no_saved_words"},
    {"Look up", "home.look_up"},
    {"Transcribe note", "home.transcribe_note"},
    {"Preview unavailable", "home.preview_unavailable"},
    {"Untitled book", "home.untitled_book"},
    {"Nothing to look up.", "home.nothing_to_look_up"},
    {"No dictionary selected. Pick one in Settings > Reader > Choose dictionary.", "home.no_dictionary_selected"},
    {"Could not open the selected dictionary.", "home.dictionary_open_failed"},
    {"No definition found.", "home.no_definition"},
    {"Looking up...", "home.looking_up"},
    {"Saved", "home.saved"},
    {"Next", "action.next"},
    {"Note", "home.note"},
    {"Folders", "library.folders"},
    {"All books", "library.all_books"},
    {"Reading", "library.reading"},
    {"Finished", "library.finished"},
    {"Author", "library.author"},
    {"Title", "library.title"},
    {"Folder", "library.folder"},
    {"A-Z", "library.az"},
    {"Z-A", "library.za"},
    {"EPUB", "library.epub"},
    {"PDF", "library.pdf"},
    {"TXT", "library.txt"},
    {"XTC", "library.xtc"},
    {"All", "library.all"},
    {"Type", "library.type"},
    {"Options", "library.options"},
    {"Hide finished books", "library.hide_finished"},
    {"Yes", "action.yes"},
    {"No", "action.no"},
    {"Mark as favorite", "library.mark_favorite"},
    {"Delete Book", "library.delete_book"},
    {"Reset", "action.reset"},
    {"WiFi Networks", "wifi.networks"},
    {"Searching for connections...", "wifi.searching"},
    {"No networks found", "wifi.no_networks"},
    {"Press Connect to scan again", "wifi.scan_again"},
    {"(Locked)", "wifi.locked"},
    {"Connect", "action.connect"},
    {"Connecting to", "wifi.connecting"},
    {"Please wait...", "wifi.please_wait"},
    {"Network:", "wifi.network"},
    {"Continue", "action.continue"},
    {"Forget network and remove saved password?", "wifi.forget_prompt"},
    {"Forget network", "wifi.forget"},
    {"MAC address: %02x-%02x-%02x-%02x-%02x-%02x", "wifi.mac_address"},
    {"%zu networks found", "wifi.networks_found"},
    {"Hotspot", "wifi.hotspot"},
    {"Could not start hotspot", "wifi.hotspot_start_failed"},
    {"Press Back to try again", "wifi.try_again"},
    {"STEP 1", "wifi.step_one"},
    {"Join WiFi", "wifi.join_wifi"},
    {"STEP 2", "wifi.step_two"},
    {"Open Transfer", "wifi.open_transfer"},
    {"Local Network", "wifi.local_network"},
    {"Could not start server", "wifi.server_start_failed"},
    {"OPEN TRANSFER", "wifi.open_transfer_title"},
    {"Keep this screen open while transferring", "wifi.keep_open"},
    {"Press Back to return", "wifi.press_back_return"},
    {"CALIBRE", "wifi.calibre"},
    {"Download and install", "wifi.download_and_install"},
    {"CrossPoint Calibre", "wifi.crosspoint_calibre"},
    {"plugin", "wifi.plugin"},
    {"Choose Send to device in Calibre", "wifi.calibre_instruction"},
    {"Loading languages...", "language.loading"},
    {"Please wait", "language.please_wait"},
    {"Tap a language to download it.", "language.tap_to_download"},
    {"No language packages found.", "language.no_packages"},
    {"Could not load language packages.", "language.load_failed"},
    {"Downloading and installing...", "language.downloading"},
    {"Could not start download.", "language.download_start_failed"},
    {"DOWNLOADING LANGUAGE", "language.downloading_title"},
    {"Installing language package", "language.installing"},
    {"Language installed.", "language.installed"},
    {"Language selected.", "language.selected"},
    {"Could not select language.", "language.select_failed"},
    {"Language removed.", "language.removed"},
    {"Language removal failed.", "language.remove_failed"},
    {"Language installation failed.", "language.install_failed"},
    {"Wi-Fi connection failed. Tap Retry.", "language.wifi_failed"},
    {"Open", "action.open"},
    {"Page -", "action.page_previous"},
    {"Page +", "action.page_next"},
    {"\xC2\xAB Back", "action.back"},
    {"Display ", "settings.display"},
    {"Device ", "settings.device"},
    {"Sleep Screen", "settings.sleep_screen"},
    {"Hide Battery %", "settings.hide_battery"},
    {"Keyboard", "settings.keyboard"},
    {"Text size", "settings.text_size"},
    {"Hide title for thumbnails", "settings.hide_thumbnail_titles"},
    {"Thumbnail size", "settings.thumbnail_size"},
    {"Face", "settings.clock_face"},
    {"Format", "settings.clock_format"},
    {"Cover Mode", "settings.cover_mode"},
    {"Cover Filter", "settings.cover_filter"},
    {"Sleep Image Quality", "settings.sleep_image_quality"},
    {"Thumbnail corners", "settings.thumbnail_corners"},
    {"Flick page turn", "settings.flick_page_turn"},
    {"Flick sensitivity", "settings.flick_sensitivity"},
    {"Short Press Power Button", "settings.short_power_button"},
    {"Dark", "value.dark"},
    {"Light", "value.light"},
    {"Custom", "value.custom"},
    {"Recent Book", "value.recent_book"},
    {"Transparent Cover", "value.transparent_cover"},
    {"Date Time", "value.date_time"},
    {"Widget", "value.widget"},
    {"Never", "value.never"},
    {"In Reader", "value.in_reader"},
    {"Always", "value.always"},
    {"Small", "value.small"},
    {"Medium", "value.medium"},
    {"Large", "value.large"},
    {"Actual", "value.actual"},
    {"Even", "value.even"},
    {"12 hour", "value.12_hour"},
    {"24 hour", "value.24_hour"},
    {"Fill", "value.fill"},
    {"Crop", "value.crop"},
    {"Contrast", "value.contrast"},
    {"Inverted", "value.inverted"},
    {"Low", "value.low"},
    {"High", "value.high"},
    {"Square", "value.square"},
    {"Rounded", "value.rounded"},
    {"Subtle", "value.subtle"},
    {"1 min", "value.1_min"},
    {"5 min", "value.5_min"},
    {"10 min", "value.10_min"},
    {"15 min", "value.15_min"},
    {"30 min", "value.30_min"},
    {"Home Page", "value.home_page"},
    {"Normal", "value.normal"},
    {"Sleep", "value.sleep"},
    {"Refresh", "value.refresh"},
    {"Button & Gestures", "menu.button_gestures"},
    {"Font Family", "menu.font_family"},
    {"Text Anti-Aliasing", "menu.text_antialiasing"},
    {"Sleep Screen", "menu.sleep_screen"},
    {"Time to Sleep", "menu.time_to_sleep"},
    {"Boot Mode", "menu.boot_mode"},
    {"Theme", "menu.theme"},
    {"Display", "menu.display"},
    {"Clock", "menu.clock"},
    {"Image", "menu.image"},
    {"Actions", "menu.actions"},
    {"Quick Actions", "menu.quick_actions"},
    {"Refresh Frequency", "menu.refresh_frequency"},
    {"Page Auto Turn", "menu.page_auto_turn"},
    {"Image Quality", "menu.image_quality"},
    {"Daily Reading Goal", "menu.daily_reading_goal"},
    {"Hyphenation", "menu.hyphenation"},
    {"Preset", "menu.preset"},
    {"No actions available.", "action.no_actions"},
    {"Name this preset", "preset.name_prompt"},
    {"Rename preset", "preset.rename_prompt"},
    {"Save new preset?", "preset.save_new"},
    {"Delete Cache", "action.delete_cache"},
    {"Generate thumbnails", "action.generate_thumbnails"},
    {"Generate Authors", "action.generate_authors"},
    {"New preset +", "action.new_preset"},
    {"Choose sleep image", "action.choose_sleep_image"},
    {"Save", "action.save"},
    {"Discard", "action.discard"},
    {"Cancel", "action.cancel"},
    {"Edit", "action.edit"},
    {"Rename", "action.rename"},
    {"Delete", "action.delete"},
    {"Default", "action.default"},
    {"Retry", "action.retry"},
    {"Download", "action.download"},
    {"Back", "action.back"},
    {"Up", "action.up"},
    {"Down", "action.down"},
    {"Select", "action.select"},
    {"Toggle", "action.toggle"},
    {"Button", "settings.button"},
    {"Gestures", "settings.gestures"},
    {"Set up quick actions menu", "settings.quick_actions_setup"},
    {"Disable light control", "settings.disable_light_control"},
    {"Page turn", "settings.page_turn"},
    {"Double tap", "settings.double_tap"},
    {"Swipe", "value.swipe"},
    {"Tap", "value.tap"},
    {"Table of Content", "reader_action.table_of_content"},
    {"Left", "button.left"},
    {"Right", "button.right"},
    {"Up (Short)", "button.up_short"},
    {"Up (Long)", "button.up_long"},
    {"Down (Short)", "button.down_short"},
    {"Down (Long)", "button.down_long"},
    {"Left (Short)", "button.left_short"},
    {"Left (Long)", "button.left_long"},
    {"Right (Short)", "button.right_short"},
    {"Right (Long)", "button.right_long"},
    {"Power", "button.power"},
    {"Style", "reader_setting.style"},
    {"Size", "reader_setting.size"},
    {"Alignment", "reader_setting.alignment"},
    {"Extra Small", "value.extra_small"},
    {"X Large", "value.x_large"},
    {"Justify", "value.justify"},
    {"Center", "value.center"},
    {"Book's style", "value.book_style"},
    {"Portrait", "value.portrait"},
    {"Landscape CW", "value.landscape_cw"},
    {"Landscape CCW", "value.landscape_ccw"},
    {"Line height", "reader_setting.line_height"},
    {"Word spacing", "reader_setting.word_spacing"},
    {"Extra Paragraph Spacing", "reader_setting.extra_paragraph_spacing"},
    {"Indent", "reader_setting.indent"},
    {"Screen Margin", "reader_setting.screen_margin"},
    {"Orientation", "reader_setting.orientation"},
    {"Bionic Reading", "reader_setting.bionic_reading"},
    {"Dark mode", "reader_setting.dark_mode"},
    {"Guide Lines", "reader_setting.guide_lines"},
    {"Grid", "value.grid"},
    {"Notebook", "value.notebook"},
    {"Left Section", "reader_setting.left_section"},
    {"Middle Section", "reader_setting.middle_section"},
    {"Right Section", "reader_setting.right_section"},
    {"Full Bar", "reader_setting.full_bar"},
    {"Page Numbers", "status.page_numbers"},
    {"Percentage", "status.percentage"},
    {"Chapter Title", "status.chapter_title"},
    {"Battery Icon", "status.battery_icon"},
    {"Battery %", "status.battery_percentage"},
    {"Battery Icon+%", "status.battery_icon_percentage"},
    {"Progress Bar", "status.progress_bar"},
    {"Progress Bar+%", "status.progress_bar_percentage"},
    {"Page Bars", "status.page_bars"},
    {"Book Title", "status.book_title"},
    {"Author Name", "status.author_name"},
    {"Page Num+%", "status.page_numbers_percentage"},
    {"Time Left (Chapter)", "status.time_left_chapter"},
    {"Time Left (Book)", "status.time_left_book"},
    {"The quick brown fox jumps over the lazy dog while the printing press hums softly.", "preview.paragraph_one"},
    {"Good typography is invisible.", "preview.paragraph_two"},
    {"Chapter Three", "preview.chapter_three"},
    {"The Example Book", "preview.example_book"},
    {"Jane Author", "preview.jane_author"},
    {"First list item here", "preview.first_list_item"},
    {"Second list item too", "preview.second_list_item"},
    {"None", "reader_action.none"},
    {"Page Next", "reader_action.page_next"},
    {"Page Previous", "reader_action.page_previous"},
    {"Open Settings", "reader_action.open_settings"},
    {"Annotate", "reader_action.annotate"},
    {"Dictionary", "reader_action.dictionary"},
    {"Page Refresh", "reader_action.page_refresh"},
    {"Chapter Skip Next", "reader_action.chapter_skip_next"},
    {"Chapter Skip Previous", "reader_action.chapter_skip_previous"},
    {"Bookmark", "reader_action.bookmark"},
    {"Table of Contents", "reader_action.table_of_contents"},
    {"Change Orientation", "reader_action.change_orientation"},
    {"Apply Preset", "reader_action.apply_preset"},
    {"Generate Full Data", "reader_action.generate_full_data"},
    {"Generate Thumbnail", "reader_action.generate_thumbnail"},
    {"Go to Percent", "reader_action.go_to_percent"},
    {"Toggle Light", "reader_action.toggle_light"},
    {"Off", "value.off"},
    {"1 page", "value.1_page"},
    {"5 pages", "value.5_pages"},
    {"10 pages", "value.10_pages"},
    {"15 pages", "value.15_pages"},
    {"30 pages", "value.30_pages"},
    {"10 sec", "value.10_sec"},
    {"20 sec", "value.20_sec"},
    {"30 sec", "value.30_sec"},
    {"40 sec", "value.40_sec"},
    {"50 sec", "value.50_sec"},
    {"60 sec", "value.60_sec"},
    {"70 sec", "value.70_sec"},
    {"80 sec", "value.80_sec"},
    {"90 sec", "value.90_sec"},
    {"100 sec", "value.100_sec"},
    {"110 sec", "value.110_sec"},
    {"120 sec", "value.120_sec"},
    {"130 sec", "value.130_sec"},
    {"140 sec", "value.140_sec"},
    {"150 sec", "value.150_sec"},
    {"160 sec", "value.160_sec"},
    {"170 sec", "value.170_sec"},
    {"180 sec", "value.180_sec"},
    {"35 min", "value.35_min"},
    {"40 min", "value.40_min"},
    {"45 min", "value.45_min"},
    {"50 min", "value.50_min"},
    {"55 min", "value.55_min"},
    {"60 min", "value.60_min"},
    {"65 min", "value.65_min"},
    {"70 min", "value.70_min"},
    {"75 min", "value.75_min"},
    {"80 min", "value.80_min"},
    {"85 min", "value.85_min"},
    {"90 min", "value.90_min"},
    {"95 min", "value.95_min"},
    {"100 min", "value.100_min"},
    {"105 min", "value.105_min"},
    {"110 min", "value.110_min"},
    {"115 min", "value.115_min"},
    {"120 min", "value.120_min"},
    {"ON", "value.on"},
    {"OFF", "value.off"},
    {"On", "value.on"},
};

std::string trim(const std::string& input) {
  size_t first = 0;
  while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
  size_t last = input.size();
  while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
  return input.substr(first, last - first);
}

bool parseScalar(const std::string& source, std::string& output) {
  const std::string value = trim(source);
  if (value.empty()) {
    output.clear();
    return true;
  }
  if (value.front() != '"') {
    output = value;
    return true;
  }

  output.clear();
  bool escaped = false;
  for (size_t i = 1; i < value.size(); ++i) {
    const char c = value[i];
    if (escaped) {
      switch (c) {
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        default:
          output.push_back(c);
          break;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return true;
    } else {
      output.push_back(c);
    }
  }
  return false;
}

bool parseTranslationLine(const std::string& line, std::string& key, std::string& value) {
  const std::string content = trim(line);
  if (content.empty() || content.front() == '#') return false;
  const size_t colon = content.find(':');
  if (colon == std::string::npos) return false;
  if (!parseScalar(content.substr(0, colon), key) || key.empty()) return false;
  return parseScalar(content.substr(colon + 1), value);
}

bool loadLanguageFile(const std::string& code, std::vector<Translation>& loaded, std::string& name) {
  if (!SdMan.ready() || code.empty()) return false;

  const std::string path = "/system/lang/" + code + "/translate.yml";
  SdIoMutex::Lock ioLock;
  FsFile file = SdMan.open(path.c_str(), O_READ);
  if (!file) return false;

  loaded.clear();
  name.clear();
  bool inTranslations = false;
  std::string line;
  char buffer[512];
  while (file.available()) {
    const int read = file.read();
    if (read < 0) break;
    const char c = static_cast<char>(read);
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.size() < sizeof(buffer) - 1) line.push_back(c);
      continue;
    }

    const std::string content = trim(line);
    if (content == "translations:") {
      inTranslations = true;
    } else if (inTranslations) {
      std::string key;
      std::string value;
      if (parseTranslationLine(line, key, value)) loaded.push_back({std::move(key), std::move(value)});
    } else if (content.rfind("name:", 0) == 0) {
      parseScalar(content.substr(5), name);
    }
    line.clear();
  }
  if (!line.empty()) {
    const std::string content = trim(line);
    if (inTranslations) {
      std::string key;
      std::string value;
      if (parseTranslationLine(line, key, value)) loaded.push_back({std::move(key), std::move(value)});
    } else if (content.rfind("name:", 0) == 0) {
      parseScalar(content.substr(5), name);
    }
  }
  file.close();
  return true;
}

const char* findTranslation(const char* key) {
  if (!key || key[0] == '\0') return nullptr;
  for (const Translation& translation : translations) {
    if (translation.key == key) return translation.value.c_str();
  }
  return nullptr;
}

const char* findFallbackKey(const char* text) {
  if (!text) return nullptr;
  for (const FallbackKey& fallback : kFallbackKeys) {
    if (std::strcmp(fallback.text, text) == 0) return fallback.key;
  }
  return nullptr;
}

bool validCode(const char* code) {
  if (!code || code[0] == '\0') return true;
  const size_t length = std::strlen(code);
  if (length > 32 || !std::isalnum(static_cast<unsigned char>(code[0]))) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(code[i]);
    if (!std::isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool loadActive(const std::string& code) {
  if (code.empty()) {
    translations.clear();
    activeCode.clear();
    activeName.clear();
    initialized = true;
    return true;
  }

  std::vector<Translation> loaded;
  std::string name;
  if (!loadLanguageFile(code, loaded, name)) return false;
  translations = std::move(loaded);
  activeCode = code;
  activeName = name.empty() ? code : name;
  initialized = true;
  return true;
}

}  // namespace

void LanguageManager::initialize() {
  if (initialized && activeCode == SETTINGS.languageCode) return;
  if (SETTINGS.languageCode[0] == '\0') {
    loadActive("");
    return;
  }
  if (!loadActive(SETTINGS.languageCode)) {
    // A removed or malformed package must never make the system UI unusable.
    SETTINGS.languageCode[0] = '\0';
    loadActive("");
  }
}

bool LanguageManager::setLanguage(const char* code) {
  if (!validCode(code)) return false;
  const std::string requested = code ? code : "";
  if (!requested.empty() && !isInstalled(requested.c_str())) return false;
  if (!loadActive(requested)) return false;

  std::strncpy(SETTINGS.languageCode, requested.c_str(), sizeof(SETTINGS.languageCode) - 1);
  SETTINGS.languageCode[sizeof(SETTINGS.languageCode) - 1] = '\0';
  SETTINGS.saveToFile();
  return true;
}

const char* LanguageManager::activeLanguageCode() {
  initialize();
  return activeCode.c_str();
}

const char* LanguageManager::activeLanguageName() {
  initialize();
  return activeName.c_str();
}

bool LanguageManager::isInstalled(const char* code) {
  if (!validCode(code) || !code || code[0] == '\0' || !SdMan.ready()) return false;
  const std::string path = "/system/lang/" + std::string(code) + "/translate.yml";
  SdIoMutex::Lock ioLock;
  return SdMan.exists(path.c_str());
}

std::vector<LanguageManager::LanguageInfo> LanguageManager::installedLanguages() {
  std::vector<LanguageInfo> result;
  result.push_back({"", "English"});
  if (!SdMan.ready()) return result;

  FsFile root = SdMan.open("/system/lang");
  if (!root || !root.isDirectory()) return result;

  for (FsFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) continue;
    char entryName[128] = {0};
    entry.getName(entryName, sizeof(entryName));
    std::string code = entryName;
    const size_t slash = code.find_last_of('/');
    if (slash != std::string::npos) code = code.substr(slash + 1);
    if (!validCode(code.c_str()) || code.empty()) continue;

    std::vector<Translation> ignored;
    std::string name;
    if (!loadLanguageFile(code, ignored, name)) continue;
    result.push_back({code, name.empty() ? code : name});
  }

  std::sort(result.begin() + 1, result.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
    return a.code < b.code;
  });
  return result;
}

std::string LanguageManager::languageNameForCode(const char* code) {
  const std::string requested = code ? code : "";
  if (requested.empty()) return "English";
  for (const LanguageInfo& language : installedLanguages()) {
    if (language.code == requested) return language.name;
  }
  return requested;
}

void LanguageManager::setBookLanguage(const char* code) {
  const std::string requested = code ? code : "";
  bookCode = validCode(requested.c_str()) ? requested : "";
}

const char* LanguageManager::bookLanguageCode() {
  initialize();
  return bookCode.empty() ? activeCode.c_str() : bookCode.c_str();
}

const char* LanguageManager::translateText(const char* englishText) {
  if (!englishText) return englishText;
  initialize();
  if (activeCode.empty()) return englishText;

  // Also accept a key directly for future call sites that use semantic keys.
  if (const char* translated = findTranslation(englishText)) return translated;
  if (const char* key = findFallbackKey(englishText)) {
    if (const char* translated = findTranslation(key)) return translated;
  }
  return englishText;
}
