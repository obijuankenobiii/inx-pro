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
    {"Mark as completed", "library.mark_completed"},
    {"Delete Book", "library.delete_book"},
    {"Favorites", "library.favorites"},
    {"No items in this plugin view", "library.no_plugin_items"},
    {"Generate authors in Settings first", "library.generate_authors_first"},
    {"No books in this folder", "library.no_books_in_folder"},
    {"Build the library index first", "library.build_index_first"},
    {"No matching books", "search.no_matching_books"},
    {"View description", "home.view_description"},
    {"Tap a heatmap day", "heatmap.tap_day"},
    {"No books recorded", "heatmap.no_books_recorded"},
    {"Books read", "statistics.books_read"},
    {"of your books", "statistics.of_your_books"},
    {"are finished.", "statistics.are_finished"},
    {"Books finished", "statistics.books_finished"},
    {"Books opened", "statistics.books_opened"},
    {"No recent book", "statistics.no_recent_book"},
    {"Book progress: %.0f%%", "statistics.book_progress"},
    {"Reset", "action.reset"},
    {"OK", "action.ok"},
    {"Install", "action.install"},
    {"Disable", "action.disable"},
    {"WiFi Networks", "wifi.networks"},
    {"Enter WiFi Password", "wifi.enter_password"},
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
    {"[Cancel]", "wifi.cancel_selected"},
    {"[Forget network]", "wifi.forget_selected"},
    {"Error: General failure", "wifi.error_general"},
    {"Error: Network not found", "wifi.error_network_not_found"},
    {"Error: Connection timeout", "wifi.error_timeout"},
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
    {"OPDS Server", "opds.server"},
    {"Add OPDS server", "opds.add_server"},
    {"OPDS Server Name", "opds.server_name"},
    {"OPDS Server URL", "opds.server_url"},
    {"OPDS Username", "opds.username"},
    {"OPDS Password", "opds.password"},
    {"Checking WiFi...", "opds.checking_wifi"},
    {"Loading...", "opds.loading"},
    {"Error:", "opds.error"},
    {"Downloading...", "opds.downloading"},
    {"No entries found", "opds.no_entries"},
    {"No server URL configured", "opds.no_server_url"},
    {"Failed to fetch feed", "opds.fetch_failed"},
    {"Failed to parse feed", "opds.parse_failed"},
    {"Download failed", "opds.download_failed"},
    {"WiFi connection failed", "opds.wifi_failed"},
    {"Create backup", "backup.create"},
    {"Restore backup", "backup.restore"},
    {"Creating backup", "backup.creating"},
    {"Restoring backup", "backup.restoring"},
    {"Backup complete", "backup.complete"},
    {"Backup incomplete", "backup.incomplete"},
    {"Restore complete", "backup.restore_complete"},
    {"Restore incomplete", "backup.restore_incomplete"},
    {"Saved in /.system/backup", "backup.saved"},
    {"State restored from backup", "backup.restored"},
    {"Some files could not be copied", "backup.copy_failed"},
    {"%d copied, %d skipped, %d failed", "backup.copy_summary"},
    {"Menu", "action.menu"},
    {"KOReader", "koreader.title"},
    {"Username", "koreader.username"},
    {"Password", "koreader.password"},
    {"Sync Server URL", "koreader.server_url"},
    {"Document Matching", "koreader.document_matching"},
    {"Sign Up", "koreader.sign_up"},
    {"Authenticate", "koreader.authenticate"},
    {"Not Set", "koreader.not_set"},
    {"Set", "koreader.set"},
    {"Filename", "koreader.filename"},
    {"Binary", "koreader.binary"},
    {"Set credentials first", "koreader.credentials_required"},
    {"KOReader Username", "koreader.username_prompt"},
    {"KOReader Password", "koreader.password_prompt"},
    {"KOReader Sign Up", "koreader.sign_up_title"},
    {"KOReader Auth", "koreader.auth_title"},
    {"Creating account...", "koreader.creating_account"},
    {"Authenticating...", "koreader.authenticating"},
    {"Account created!", "koreader.account_created"},
    {"Successfully authenticated!", "koreader.authenticated"},
    {"Username is already registered", "koreader.username_exists"},
    {"Success!", "koreader.success"},
    {"KOReader sync is ready to use", "koreader.ready"},
    {"Sign-up failed", "koreader.sign_up_failed"},
    {"Authentication Failed", "koreader.auth_failed"},
    {"Done", "action.done"},
    {"Update", "ota.update"},
    {"INSTALLING UPDATE", "ota.installing_title"},
    {"Installing firmware", "ota.installing_firmware"},
    {"Please keep the device powered on.", "ota.keep_powered"},
    {"Preparing package", "ota.preparing"},
    {"Online update", "ota.online"},
    {"SD card firmware", "ota.sd_firmware"},
    {"Choose a network above.", "ota.choose_network"},
    {"This may take a moment.", "ota.wait"},
    {"Firmware package", "ota.package"},
    {"CURRENT VERSION", "ota.current_version"},
    {"AVAILABLE UPDATE", "ota.available_update"},
    {"PACKAGE", "ota.package_label"},
    {"No firmware .bin files found.", "ota.no_files"},
    {"Put .bin files in / or /firmware.", "ota.put_files"},
    {"SD FIRMWARE", "ota.sd_firmware_title"},
    {"Install update?", "ota.install_question"},
    {"Keep the device powered on during install.", "ota.keep_powered_install"},
    {"Firmware file is missing.", "ota.file_missing"},
    {"No update available", "ota.no_update"},
    {"Update failed", "ota.failed"},
    {"Update complete", "ota.complete"},
    {"Press and hold power button to turn back on", "ota.power_cycle"},
    {"Choose dictionary", "dictionary.choose"},
    {"No dictionaries found.", "dictionary.none"},
    {"Put StarDict folders under /dictionaries/", "dictionary.install_hint"},
    {"FIRMWARE", "device_info.firmware"},
    {"BOARD", "device_info.board"},
    {"DISPLAY", "device_info.display"},
    {"CHIP", "device_info.chip"},
    {"FLASH", "device_info.flash"},
    {"PSRAM", "device_info.psram"},
    {"FREE HEAP", "device_info.free_heap"},
    {"ESP-IDF", "device_info.esp_idf"},
    {"MAC ADDRESS", "device_info.mac_address"},
    {"Unknown", "device_info.unknown"},
    {"Select timezone", "time.select_timezone"},
    {"Sync time", "time.sync"},
    {"Search timezone", "time.search_timezone"},
    {"No matching timezones", "time.no_matching_timezones"},
    {"Select network", "time.select_network"},
    {"Loading timezones", "time.loading_timezones"},
    {"Time synced", "time.synced"},
    {"Time sync failed", "time.sync_failed"},
    {"Select a WiFi network", "time.select_wifi"},
    {"WiFi was not connected", "time.wifi_not_connected"},
    {"Downloading complete timezone list", "time.downloading_timezones"},
    {"Could not load timezones", "time.load_failed"},
    {"Automatic (network)", "time.automatic_network"},
    {"Applying timezone", "time.applying_timezone"},
    {"Could not apply timezone", "time.apply_failed"},
    {"Syncing from pool.ntp.org", "time.syncing"},
    {"NTP did not return time", "time.ntp_failed"},
    {"%02u:%02u saved to RTC", "time.saved_to_rtc"},
    {"%02u:%02u synced", "time.synced_at"},
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
    {"Store", "menu.store"},
    {"Plugin Manager", "menu.plugin_manager"},
    {"Font", "store.font"},
    {"Browse and install reading fonts", "store.font_description"},
    {"Add translations and system languages", "store.language_description"},
    {"Dictionary", "store.dictionary"},
    {"Browse and install dictionaries", "store.dictionary_description"},
    {"Plugins", "store.plugins"},
    {"Explore optional reading extensions", "store.plugins_description"},
    {"Coming soon...", "store.coming_soon"},
    {"Anki Export", "plugin.study_cards.name"},
    {"Adds anki supported export", "plugin.study_cards.description"},
    {"Add to study", "plugin.study_cards.add"},
    {"Series", "plugin.series.name"},
    {"Group books into reading series", "plugin.series.description"},
    {"Group books into reading series and open the next book.", "plugin.series.description_full"},
    {"Open next book", "plugin.series.open_next"},
    {"Tap a plugin to install it.", "plugin.tap_to_install"},
    {"No plugins available.", "plugin.no_plugins"},
    {"Plugin disabled. Card data was kept.", "plugin.disabled"},
    {"Plugin removal failed.", "plugin.remove_failed"},
    {"Plugin installed.", "plugin.installed"},
    {"Plugin installation failed.", "plugin.install_failed"},
    {"Could not start download.", "plugin.download_start_failed"},
    {"Downloading and installing...", "plugin.downloading"},
    {"Series", "plugin.series.web.title"},
    {"No series yet", "plugin.series.web.no_series"},
    {"New series name", "plugin.series.web.new_series_name"},
    {"New series", "plugin.series.web.new_series"},
    {"Refresh", "plugin.series.web.refresh"},
    {"Building library index…", "plugin.series.web.building_index"},
    {"Loading library index…", "plugin.series.web.loading_index"},
    {"Library index is not available. Build it before grouping books.", "plugin.series.web.index_unavailable"},
    {"The library index is not available.", "plugin.series.web.index_unavailable_short"},
    {"Build library index", "plugin.series.web.build_index"},
    {"Books", "plugin.series.web.books"},
    {"Collapse", "plugin.series.web.collapse"},
    {"Show books", "plugin.series.web.show_books"},
    {"Search books", "plugin.series.web.search_books"},
    {"Choose or create a series", "plugin.series.web.choose_series"},
    {"Choose an existing series or create one, then drag books here.", "plugin.series.web.empty_prompt"},
    {"Drag books to set the order", "plugin.series.web.drag_order"},
    {"Save changes", "plugin.series.web.save_changes"},
    {"No matching books.", "plugin.series.web.no_matching_books"},
    {"In this series", "plugin.series.web.in_series"},
    {"Not in a series", "plugin.series.web.not_in_series"},
    {"Add book", "plugin.series.web.add_book"},
    {"Remove book", "plugin.series.web.remove_book"},
    {"Drop books here to build this series.", "plugin.series.web.drop_books"},
    {"Saving series…", "plugin.series.web.saving"},
    {"Series saved.", "plugin.series.web.saved"},
    {"Series created", "plugin.series.web.created"},
    {"Create a series to begin.", "plugin.series.web.create_prompt"},
    {"Drag books into a series, reorder them, then save once.", "plugin.series.web.drag_prompt"},
    {"Enter a series name first.", "plugin.series.web.name_required"},
    {"Discard unsaved series changes?", "plugin.series.web.discard_prompt"},
    {"Library index refresh is still running. Tap again when it finishes.", "plugin.series.web.refresh_running"},
    {"Anki Export", "plugin.study_cards.web.title"},
    {"Loading...", "plugin.study_cards.web.loading_summary"},
    {"Download JSON", "plugin.study_cards.web.download_json"},
    {"Download for Anki", "plugin.study_cards.web.download_anki"},
    {"Loading cards...", "plugin.study_cards.web.loading"},
    {"Front", "plugin.study_cards.web.front"},
    {"Back / answer", "plugin.study_cards.web.back"},
    {"Write the answer shown on the back of this card", "plugin.study_cards.web.answer_placeholder"},
    {"Save answer", "plugin.study_cards.web.save_answer"},
    {"Saved", "plugin.study_cards.web.saved"},
    {"No study cards yet. Select text in a book and choose “Add to study”.", "plugin.study_cards.web.empty"},
    {"Unavailable", "plugin.study_cards.web.unavailable"},
    {"Could not load cards", "plugin.study_cards.web.load_failed"},
    {"Could not save answer", "plugin.study_cards.web.save_failed"},
    {"Could not export cards", "plugin.study_cards.web.export_failed"},
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
    {"Prev", "action.previous"},
    {"Up", "action.up"},
    {"Down", "action.down"},
    {"SPACE", "keyboard.space"},
    {"DEL", "keyboard.delete"},
    {"CAPS", "keyboard.caps"},
    {"SHIFT", "keyboard.shift"},
    {"NUM", "keyboard.num"},
    {"SYMBOL", "keyboard.symbol"},
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

  const std::string path = "/.system/lang/" + code + "/translate.yml";
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
  const std::string path = "/.system/lang/" + std::string(code) + "/translate.yml";
  SdIoMutex::Lock ioLock;
  return SdMan.exists(path.c_str());
}

std::vector<LanguageManager::LanguageInfo> LanguageManager::installedLanguages() {
  std::vector<LanguageInfo> result;
  result.push_back({"", "English"});
  if (!SdMan.ready()) return result;

  FsFile root = SdMan.open("/.system/lang");
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
