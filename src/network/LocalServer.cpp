/**
 * @file LocalServer.cpp
 * @brief Definitions for LocalServer.
 */

#include "LocalServer.h"

#include <ArduinoJson.h>
#include <uri/UriGlob.h>
#ifndef INX_SIMULATOR_WEB_ONLY
#include <Epub.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#endif
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <BoardConfig.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "../state/ReaderSetting.h"
#include "../state/SystemSetting.h"
#include "../system/LanguageManager.h"
#include "../system/PluginManager.h"
#ifndef INX_SIMULATOR_WEB_ONLY
#include "activity/reader/Epub/GeminiTranscription.h"
#endif
#include "html/EpubPageHtml.generated.h"
#include "html/EpubPageJs.generated.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FilesPageJs.generated.h"
#include "html/FontManagerPageHtml.generated.h"
#include "html/LanguageManagerPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/InxFontPackJs.generated.h"
#include "html/JsZipMinJs.generated.h"
#include "html/QrCreatorLogoJs.generated.h"
#include "html/SettingsPageHtml.generated.h"
#ifndef INX_SIMULATOR_WEB_ONLY
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "system/FontManager.h"
#include "util/StringUtils.h"
#endif
#include "KOReaderCredentialStore.h"
#include "state/NetworkCredential.h"
#ifndef INX_SIMULATOR_WEB_ONLY
#include "state/OpdsServerStore.h"
#include "util/LibraryIndex.h"
#include "util/LibraryIndexRefresh.h"
#endif

class Activity;
class GfxRenderer;
extern Activity* currentActivity;
extern GfxRenderer& render;

namespace {

const char* HIDDEN_ITEMS[] = {"System Volume Information", ".metadata"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;
constexpr const char* DEVICE_IDENTITY_DIR = "/.system/identity";
constexpr const char* DEVICE_IDENTITY_JSON = "/.system/identity/device.json";
constexpr const char* DEVICE_IDENTITY_PHOTO = "/.system/identity/device-photo.png";
constexpr const char* DEVICE_IDENTITY_CARD = "/sleep/device-identity.jpg";

LocalServer* wsInstance = nullptr;

FsFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String escapeHtml(const std::string& value) {
  String escaped;
  for (const char c : value) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

bool findWebPluginForUri(const String& uri, PluginManager::WebLink& result) {
  std::vector<PluginManager::WebLink> links;
  if (!PluginManager::listWebPlugins(links)) return false;

  for (const PluginManager::WebLink& link : links) {
    if (uri == link.path.c_str()) {
      result = link;
      return true;
    }
    if (uri.startsWith("/plugin/") && uri.substring(8) == link.id.c_str()) {
      result = link;
      return true;
    }
  }
  return false;
}

String addLanguageManagerNavLink(const char* pageHtml, const char* currentUri = nullptr) {
  String page = pageHtml;
  if (page.indexOf("/language-manager") < 0) {
    page.replace("</nav>", "<a class=nav-btn href=/language-manager>Language</a></nav>");
  }

  std::vector<PluginManager::WebLink> links;
  if (PluginManager::listWebPlugins(links)) {
    for (const PluginManager::WebLink& link : links) {
      if (page.indexOf(link.path.c_str()) >= 0) continue;
      String navLink;
      navLink += "<a class=nav-btn href=\"";
      navLink += link.path.c_str();
      navLink += "\">";
      navLink += escapeHtml(link.label);
      navLink += "</a>";
      page.replace("</nav>", navLink + "</nav>");
    }
  }

  // Shared INX web shell. Keep the page-specific HTML/JS below it intact, but
  // present the same library-style navigation and visual language everywhere.
  const char* shellStyle = R"rawliteral(
<style id="inx-web-shell">
:root{--inx-orange:#22272b;--inx-orange-dark:#000;--inx-ink:#182027;--inx-muted:#7d858b;--inx-line:#e3e5e7;--inx-page:#f5f6f7;--inx-panel:#fff;--inx-soft:#f1f2f3}
*{box-sizing:border-box}
body{background:var(--inx-page)!important;color:var(--inx-ink)!important;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif!important;min-height:100vh;overflow-x:hidden}
.inx-rail{position:fixed;z-index:900;inset:0 auto 0 0;width:68px;background:#fff;border-right:1px solid var(--inx-line);display:flex;flex-direction:column;align-items:center;padding:18px 10px;gap:9px}
.inx-brand{width:38px;height:38px;display:grid;place-items:center;margin-bottom:18px;color:var(--inx-orange);font-size:25px;font-weight:850;line-height:1}
.inx-brand:before{content:"▰";transform:skew(-14deg);display:block}
.inx-rail-link{width:42px;height:42px;display:grid;place-items:center;border:1px solid transparent;border-radius:10px;color:#758087;text-decoration:none;font-size:19px;font-weight:650;line-height:1;transition:.15s ease}.inx-rail-link svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}.inx-font-glyph{font-size:14px;letter-spacing:-.08em}
.inx-plugin-launcher{width:42px;height:42px;display:grid;place-items:center;margin-top:4px;border:1px solid transparent;border-top:1px solid var(--inx-line);border-radius:5px;background:transparent;color:#758087;cursor:pointer;padding:9px 0 0;text-decoration:none;transition:.15s ease}.inx-plugin-launcher svg{width:19px;height:19px;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}.inx-plugin-launcher:hover{color:var(--inx-orange);background:#f0f1f2}.inx-plugin-launcher.active{background:var(--inx-orange);color:#fff;box-shadow:0 5px 12px rgba(24,32,39,.18)}
.inx-rail-link:hover{color:var(--inx-orange);background:#f0f1f2}
.inx-rail-link.active{background:var(--inx-orange);color:#fff;box-shadow:0 5px 12px rgba(24,32,39,.18)}
.inx-rail-link.inx-rail-bottom{margin-top:auto}
.inx-topbar{position:fixed;z-index:850;top:0;left:68px;right:0;height:76px;background:rgba(255,255,255,.96);border-bottom:1px solid var(--inx-line);display:flex;align-items:center;justify-content:space-between;padding:0 30px 0 32px;backdrop-filter:blur(10px)}
.inx-mobile-menu-toggle{display:none;border:1px solid var(--inx-line);border-radius:5px;background:#fff;color:var(--inx-ink);width:38px;height:38px;place-items:center;cursor:pointer;padding:0}.inx-mobile-menu-toggle svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round}.inx-mobile-menu-backdrop,.inx-mobile-menu{display:none}
.inx-heading{display:flex;align-items:center;gap:18px;min-width:0}.inx-heading strong{font-size:20px;letter-spacing:-.02em}.inx-heading small{display:block;color:var(--inx-muted);font-size:11px;font-weight:500;margin-top:3px}.inx-heading .inx-slash{color:#c8c0bb;margin:0 5px}.inx-heading .inx-current{color:var(--inx-orange)}
.container{width:calc(100% - 68px)!important;max-width:none!important;margin:0 0 0 68px!important;padding:102px 32px 42px 36px!important}
.header,.nav-links{display:none!important}
.page-header{background:var(--inx-panel);border:1px solid var(--inx-line);border-radius:14px;padding:20px 22px;margin:0 0 14px!important;box-shadow:0 3px 12px rgba(31,24,20,.035)}
.breadcrumb{font-size:12px!important;color:var(--inx-muted)!important}.breadcrumb a{color:var(--inx-orange)!important}.breadcrumb .current{color:var(--inx-ink)!important}
.action-buttons{gap:8px!important}.action-btn{border-radius:8px!important;padding:9px 14px!important;font-weight:650!important}.primary-action{background:var(--inx-orange)!important;color:#fff!important}.primary-action:hover{background:var(--inx-orange-dark)!important}.secondary-action{background:#f8f7f6!important;color:#4d555a!important;border:1px solid var(--inx-line)!important}
.dropzone{border:1px dashed #e4dcd7!important;border-radius:10px!important;background:#fff!important;padding:18px 20px!important;margin-bottom:14px!important}.dropzone:hover,.dropzone.dragover{border-color:var(--inx-orange)!important;background:var(--inx-soft)!important}.dropzone-title{font-size:13px!important}.dropzone-hint{font-size:11.5px!important}
.card{border:1px solid var(--inx-line)!important;border-radius:14px!important;padding:20px 22px!important;box-shadow:0 3px 12px rgba(31,24,20,.035)!important;background:#fff!important}.contents-header{padding-bottom:15px;margin-bottom:12px!important;border-bottom:1px solid var(--inx-line)}.contents-title{font-size:18px!important;font-weight:750!important;letter-spacing:-.02em}.summary-inline{color:var(--inx-muted)!important}
.tabs{gap:4px!important}.tab-btn,.settings-section,.section-add-btn,.setting-control input,.setting-control select,.wifi-form-buttons button,.icon-btn,.wifi-form,.toast{border-radius:5px!important}.settings-section{border:1px solid var(--inx-line)!important;box-shadow:0 3px 12px rgba(31,24,32,.035)!important}.toggle-slider{border-radius:5px!important}.toggle-slider:before{border-radius:3px!important}.action-buttons .action-btn,.btn-primary,.btn-secondary{border-radius:5px!important}
.file-row{border-bottom:1px solid var(--inx-line)!important;padding:12px 7px!important}.file-row:hover{background:#f7f7f7!important}.file-row .name,.name{color:var(--inx-ink)!important}.row-action{border-radius:7px!important}.row-action:hover{background:#eef0f1!important;color:var(--inx-orange-dark)!important}.select-box{accent-color:var(--inx-orange)!important}.epub-badge,.badge{background:#eef0f1!important;color:var(--inx-orange-dark)!important}
.upload-status,.import-summary,.import-options{border-radius:10px!important;border-color:var(--inx-line)!important}.progress-fill{background:var(--inx-orange)!important}.bulk-actions{border-radius:9px!important;background:#f5f6f7!important;border-color:#e0e2e4!important}.bulk-delete-btn{border-radius:8px!important}.modal{border-radius:12px!important}.modal-btn{border-radius:8px!important}.modal-btn.primary{background:var(--inx-orange)!important}.toast{background:var(--inx-ink)!important;border-radius:8px!important}
.inx-book-toolbar{display:flex;align-items:center;justify-content:flex-end;gap:8px;margin:-2px 0 14px}.inx-book-search{height:38px;width:min(260px,38vw);border:1px solid var(--inx-line);border-radius:8px;background:#faf9f8;padding:0 12px;color:var(--inx-ink);font:inherit;font-size:12px}.inx-view-toggle{height:38px;min-width:38px;border:1px solid var(--inx-line);border-radius:8px;background:#f8f7f6;color:#697278;font:inherit;cursor:pointer}.inx-view-toggle.active{background:var(--inx-orange);border-color:var(--inx-orange);color:#fff}
.file-list.inx-grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:28px 26px}.inx-grid .book-card,.inx-grid .folder-card{position:relative;width:100%;min-width:0}.inx-grid .select-box{position:absolute;z-index:2;top:8px;left:8px;width:18px;height:18px}.inx-grid .book-open,.inx-grid .folder-open{display:block;width:100%;padding-top:30px;text-decoration:none;color:inherit}.inx-cover{width:100%;aspect-ratio:2/3;overflow:hidden;border-radius:8px;background:linear-gradient(145deg,#d9dcde,#73787c);box-shadow:0 9px 18px rgba(30,34,38,.13);display:grid;place-items:center;color:#fff;font-size:30px;font-weight:800}.inx-cover img{width:100%;height:100%;object-fit:contain;background:#f0f1f2;display:block}.inx-cover.placeholder{padding:16px;text-align:center;line-height:1.15}.inx-grid .folder-open .inx-folder-stack{width:100%;margin:0;aspect-ratio:5/4;height:auto}.inx-grid .folder-open .inx-folder-cover,.inx-grid .book-open .inx-cover{width:80%!important;max-width:80%!important;margin:0;aspect-ratio:5/4;height:auto}.inx-grid .book-open .inx-cover img{width:100%!important;height:100%!important;object-fit:contain!important;object-position:left center!important}.inx-grid .folder-open .inx-folder-cover{background:#f0f1f2}.inx-book-title{font-size:13px;font-weight:750;line-height:1.25;margin-top:9px;display:-webkit-box;-webkit-box-orient:vertical;-webkit-line-clamp:2;overflow:hidden}.inx-book-meta{font-size:11px;color:var(--inx-muted);margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.inx-card-actions{display:flex;justify-content:flex-end;gap:1px;margin-top:3px}.inx-card-actions .row-action{width:27px;height:27px}.inx-folder-cover{background:#eef0f1;color:#222;font-size:44px}.inx-grid .epub-badge{display:none}
.file-list.inx-list{display:block}.inx-list .book-card,.inx-list .folder-card{display:flex;align-items:center;gap:12px;border-bottom:1px solid var(--inx-line);padding:12px 7px}.inx-list .book-open,.inx-list .folder-open{display:flex;align-items:center;gap:12px;flex:1;min-width:0;text-decoration:none;color:inherit}.inx-list .inx-cover{width:42px;height:58px;aspect-ratio:auto;flex:0 0 42px;border-radius:5px;font-size:14px;box-shadow:0 3px 8px rgba(57,37,28,.1)}.inx-list .inx-book-title{margin:0;font-size:14px}.inx-list .inx-book-meta{margin-top:2px}.inx-list .inx-card-actions{margin:0}.inx-list .select-box{flex:0 0 auto}.inx-list .folder-open .inx-cover{font-size:23px}.inx-list .folder-open .inx-book-title{font-size:13px}
.inx-grid .book-card,.inx-grid .folder-card{max-width:288px}.file-list.inx-grid{grid-template-columns:repeat(7,minmax(0,1fr));justify-content:start}.inx-grid .inx-card-actions{justify-content:flex-end}.inx-grid .folder-open .inx-folder-cover,.inx-grid .book-open .inx-cover{width:100%!important;max-width:100%!important}
@media(max-width:760px){.inx-rail{display:none}.inx-topbar{left:0;height:64px;padding:0 14px;gap:12px}.inx-mobile-menu-toggle{display:grid;flex:0 0 38px}.inx-heading{flex:1}.inx-mobile-menu-backdrop{position:fixed;z-index:1080;inset:0;background:rgba(24,32,39,.16)}.inx-mobile-menu-backdrop.open{display:block}.inx-mobile-menu{position:fixed;z-index:1090;left:0;top:0;bottom:0;width:min(292px,86vw);display:none;background:#fff;border-right:1px solid var(--inx-line);box-shadow:8px 0 24px rgba(24,32,39,.14);padding:18px 12px}.inx-mobile-menu.open{display:block}.inx-mobile-menu-head{display:flex;align-items:center;justify-content:space-between;padding:0 6px 18px;border-bottom:1px solid var(--inx-line)}.inx-mobile-menu-head strong{font-size:18px}.inx-mobile-menu-close{border:0;background:transparent;color:var(--inx-muted);font-size:25px;line-height:1;cursor:pointer;padding:0 4px}.inx-mobile-menu-links{display:grid;gap:4px;padding-top:14px}.inx-mobile-menu-link{display:flex;align-items:center;gap:12px;padding:11px 10px;color:var(--inx-ink);text-decoration:none;border:1px solid transparent;border-radius:5px;font-size:14px;font-weight:650}.inx-mobile-menu-link svg{width:20px;height:20px;flex:0 0 20px;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}.inx-mobile-menu-link:hover,.inx-mobile-menu-link.active{background:#f0f1f2;border-color:var(--inx-line)}.container{width:100%!important;margin:0!important;padding:82px 12px 28px!important}.page-header{padding:15px!important}.action-buttons{display:flex!important;overflow:auto}.action-btn{flex:0 0 auto}.card{padding:15px!important}.inx-book-toolbar{justify-content:stretch;flex-wrap:wrap}.inx-book-search{width:100%;order:-1}.file-list.inx-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:18px 12px}.contents-header{align-items:flex-start}.summary-inline{font-size:11px}}
.inx-rail-link,.inx-book-search,.inx-view-toggle,.page-header,.card,.dropzone,.upload-status,.import-summary,.import-options,.modal,.action-btn,.bulk-actions,.bulk-delete-btn,.modal-btn,.file-input,.text-input,.stat-card,.segmented,.segmented button,.identity-add,.card-stage,.row-action,.toast,.badge,.epub-badge{border-radius:5px!important}
.inx-mobile-menu-link.active{background:var(--inx-orange)!important;color:#fff!important;border-color:var(--inx-orange)!important}
.file-row{border-radius:3px!important}.status-badge{border-radius:5px!important}.inx-cover{border-radius:5px!important}.inx-avatar{border-radius:50%!important}.inx-mobile-menu-toggle{border:0!important;border-radius:0!important;background:transparent!important;padding:0!important;aspect-ratio:1/1}.inx-mobile-menu-toggle svg{width:20px!important;height:20px!important;aspect-ratio:1/1;display:block}.inx-plugin-launcher{width:42px!important;height:42px!important;aspect-ratio:1/1;border:0!important;border-radius:5px!important;padding:0!important}.inx-plugin-launcher svg{width:19px!important;height:19px!important;aspect-ratio:1/1;display:block}.plugin-card img,.plugin-card svg{width:38px!important;height:38px!important;aspect-ratio:1/1;flex:0 0 38px;display:block;object-fit:contain}
</style>)rawliteral";
  if (page.indexOf("</head>") >= 0) {
    page.replace("</head>", String(shellStyle) + "</head>");
  } else {
    page.replace("</style>", String("</style>") + shellStyle);
  }

  String label = "Library";
  String active = "/";
  if (page.indexOf("Inx — Dashboard") >= 0 || page.indexOf("Inx - Dashboard") >= 0) {
    label = "Dashboard";
    active = "/";
  } else if (page.indexOf("Inx — Epub") >= 0 || page.indexOf("Inx - Epub") >= 0) {
    label = "Book";
    active = "/epub";
  } else if (page.indexOf("Inx - Files") >= 0) {
    label = "Files";
    active = "/files";
  } else if (page.indexOf("Inx — Fonts") >= 0 || page.indexOf("Inx - Fonts") >= 0) {
    label = "Fonts";
    active = "/font-manager";
  } else if (page.indexOf("Inx — Language Manager") >= 0 || page.indexOf("Inx - Language Manager") >= 0) {
    label = "Language";
    active = "/language-manager";
  } else if (page.indexOf("Inx — Settings") >= 0 || page.indexOf("Inx - Settings") >= 0) {
    label = "Settings";
    active = "/settings";
  } else if (page.indexOf("Inx — Plugins") >= 0 || page.indexOf("Inx - Plugins") >= 0) {
    label = "Plugins";
    active = "/plugins";
  }

  // The document title is not a reliable route identifier (plugin pages can
  // reuse titles and some pages are served from shared HTML). Prefer the
  // actual request URI whenever the handler supplied it, so the selected
  // item stays active on desktop and mobile navigation alike.
  if (currentUri != nullptr) {
    const String uri(currentUri);
    if (uri == "/") {
      label = "Dashboard";
      active = "/";
    } else if (uri == "/epub") {
      label = "Book";
      active = "/epub";
    } else if (uri == "/files") {
      label = "Files";
      active = "/files";
    } else if (uri == "/font-manager") {
      label = "Fonts";
      active = "/font-manager";
    } else if (uri == "/language-manager") {
      label = "Language";
      active = "/language-manager";
    } else if (uri == "/settings") {
      label = "Settings";
      active = "/settings";
    } else if (uri == "/plugins") {
      label = "Plugins";
      active = "/plugins";
    }
  }

  // Plugin pages are served through the same shell, but their page titles are
  // supplied by the package. Treat the matching plugin route as the active
  // destination so the shared puzzle/Plugins rail item has the same dark
  // active state as the core pages.
  String activePluginHref = "/plugins";
  String activePluginLabel = "Plugins";
  bool pluginPageActive = active == "/plugins";
  for (const PluginManager::WebLink& link : links) {
    String titleMarker = "<title>Inx — ";
    titleMarker += link.label.c_str();
    titleMarker += "</title>";
    String titleMarkerAscii = "<title>Inx - ";
    titleMarkerAscii += link.label.c_str();
    titleMarkerAscii += "</title>";
    String pluginAlias = "/plugin/";
    pluginAlias += link.id.c_str();
    const bool uriMatch = currentUri != nullptr &&
        (String(currentUri) == link.path.c_str() || String(currentUri) == pluginAlias);
    if (uriMatch || page.indexOf(titleMarker) >= 0 || page.indexOf(titleMarkerAscii) >= 0) {
      label = link.label.c_str();
      active = link.path.c_str();
      activePluginHref = link.path.c_str();
      activePluginLabel = link.label.c_str();
      pluginPageActive = true;
      break;
    }
  }

  const char* railItems[][3] = {
      {"/", "<svg viewBox=\"0 0 24 24\"><path d=\"m3 10 9-7 9 7\"/><path d=\"M5 9v11h14V9\"/><path d=\"M9 20v-6h6v6\"/></svg>", "Dashboard"},
      {"/files", "<svg viewBox=\"0 0 24 24\"><rect x=\"3\" y=\"4\" width=\"7\" height=\"7\"/><rect x=\"14\" y=\"4\" width=\"7\" height=\"7\"/><rect x=\"3\" y=\"14\" width=\"7\" height=\"7\"/><rect x=\"14\" y=\"14\" width=\"7\" height=\"7\"/></svg>", "Files"},
      {"/epub", "<svg viewBox=\"0 0 24 24\"><path d=\"M5 4.5A2.5 2.5 0 0 1 7.5 2H19v17H7.5A2.5 2.5 0 0 1 5 16.5v-12Z\"/><path d=\"M5 16.5A2.5 2.5 0 0 1 7.5 14H19\"/></svg>", "Books"},
      {"/font-manager", "<span class=inx-font-glyph>Aa</span>", "Fonts"},
      {"/language-manager", "<svg viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M3 12h18M12 3c2.3 2.5 3.4 5.5 3.4 9S14.3 18.5 12 21M12 3c-2.3 2.5-3.4 5.5-3.4 9S9.7 18.5 12 21\"/></svg>", "Language"},
      {"/settings", "<svg viewBox=\"0 0 24 24\"><path d=\"M19.43 12.98c.04-.32.07-.65.07-.98s-.02-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.37-.31-.6-.22l-2.49 1a7.4 7.4 0 0 0-1.69-.98l-.38-2.65A.51.51 0 0 0 14 2h-4a.51.51 0 0 0-.5.42l-.38 2.65c-.61.25-1.17.58-1.69.98l-2.49-1c-.23-.08-.48 0-.6.22l-2 3.46c-.12.22-.07.49.12.64l2.11 1.65c-.04.32-.07.65-.07.98s.02.66.07.98l-2.11 1.65c-.19.15-.24.42-.12.64l2 3.46c.12.22.37.31.6.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.04.24.25.42.5.42h4c.25 0 .46-.18.5-.42l.38-2.65c.61-.25 1.17-.58 1.69-.98l2.49 1c.23.08.48 0 .6-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65Z\"/><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg>", "Settings"},
  };
  String rail = "<aside class=inx-rail><a class=inx-brand href=/ aria-label=INX>▰</a>";
  for (const auto& item : railItems) {
    rail += "<a class=inx-rail-link";
    if (active == item[0]) rail += " active aria-current=page";
    rail += " href=\"";
    rail += item[0];
    rail += "\" title=\"";
    rail += item[2];
    rail += "\">";
    rail += item[1];
    rail += "</a>";
  }
  rail += "<a class=inx-plugin-launcher";
  if (pluginPageActive) rail += " active aria-current=page";
  rail += " href=\"";
  rail += activePluginHref;
  rail += "\" title=\"";
  rail += activePluginLabel;
  rail += "\" aria-label=\"";
  rail += activePluginLabel;
  rail += "\"><svg viewBox=\"0 0 24 24\"><path d=\"M19 13a2 2 0 1 0 0-4h-1V5a2 2 0 0 0-2-2h-4v1a2 2 0 1 1-4 0V3H6a2 2 0 0 0-2 2v4h1a2 2 0 1 1 0 4H4v4a2 2 0 0 0 2 2h4v-1a2 2 0 1 1 4 0v1h4a2 2 0 0 0 2-2v-4Z\"/></svg></a>";
  rail += "</aside><div class=inx-mobile-menu-backdrop id=inx-mobile-menu-backdrop></div><nav class=inx-mobile-menu id=inx-mobile-menu aria-label=Mobile navigation><div class=inx-mobile-menu-head><strong>INX</strong><button class=inx-mobile-menu-close id=inx-mobile-menu-close type=button aria-label=Close>×</button></div><div class=inx-mobile-menu-links>";
  for (const auto& item : railItems) {
    rail += "<a class=inx-mobile-menu-link";
    if (active == item[0]) rail += " active aria-current=page";
    rail += " href=\"";
    rail += item[0];
    rail += "\">";
    rail += item[1];
    rail += "<span>";
    rail += item[2];
    rail += "</span></a>";
  }
  rail += "<a class=inx-mobile-menu-link";
  if (pluginPageActive) rail += " active aria-current=page";
  rail += " href=\"";
  rail += activePluginHref;
  rail += "\"><svg viewBox=\"0 0 24 24\"><path d=\"M19 13a2 2 0 1 0 0-4h-1V5a2 2 0 0 0-2-2h-4v1a2 2 0 1 1-4 0V3H6a2 2 0 0 0-2 2v4h1a2 2 0 1 1 0 4H4v4a2 2 0 0 0 2 2h4v-1a2 2 0 1 1 4 0v1h4a2 2 0 0 0 2-2v-4Z\"/></svg><span>Plugins</span></a></div></nav><script>(function(){var b=document.getElementById('inx-mobile-menu-toggle'),m=document.getElementById('inx-mobile-menu'),o=document.getElementById('inx-mobile-menu-backdrop'),c=document.getElementById('inx-mobile-menu-close');if(!b||!m||!o)return;function close(){b.setAttribute('aria-expanded','false');m.classList.remove('open');o.classList.remove('open')}function toggle(){var open=!m.classList.contains('open');b.setAttribute('aria-expanded',open?'true':'false');m.classList.toggle('open',open);o.classList.toggle('open',open)}b.addEventListener('click',toggle);o.addEventListener('click',close);if(c)c.addEventListener('click',close);document.addEventListener('keydown',function(e){if(e.key==='Escape')close()})})();</script><header class=inx-topbar><button class=inx-mobile-menu-toggle id=inx-mobile-menu-toggle type=button aria-label=Open menu aria-expanded=false><svg viewBox=\"0 0 24 24\"><path d=\"M4 6h16M4 12h16M4 18h16\"/></svg></button><div class=inx-heading><div><strong>";
  rail += label;
  rail += "</strong><small><span class=inx-current>Dashboard</span><span class=inx-slash>/</span>";
  rail += label;
  rail += "</small></div></div></header>";
  rail += "<script>(function(){var b=document.getElementById('inx-mobile-menu-toggle'),m=document.getElementById('inx-mobile-menu'),o=document.getElementById('inx-mobile-menu-backdrop'),c=document.getElementById('inx-mobile-menu-close');if(!b||!m||!o)return;function close(){b.setAttribute('aria-expanded','false');m.classList.remove('open');o.classList.remove('open')}function toggle(){var open=!m.classList.contains('open');b.setAttribute('aria-expanded',open?'true':'false');m.classList.toggle('open',open);o.classList.toggle('open',open)}b.addEventListener('click',toggle);o.addEventListener('click',close);if(c)c.addEventListener('click',close);document.addEventListener('keydown',function(e){if(e.key==='Escape')close()})})();</script><script>(function(){function normalize(path){path=path||'/';if(path.length>1&&path.charAt(path.length-1)==='/')path=path.slice(0,-1);return path||'/'}var current=normalize(window.location.pathname);document.querySelectorAll('.inx-rail-link,.inx-plugin-launcher,.inx-mobile-menu-link').forEach(function(link){var href=link.getAttribute('href')||'/';var route=normalize(new URL(href,window.location.href).pathname);var plugin=link.classList.contains('inx-plugin-launcher')||route==='/plugins'||route.indexOf('/plugin/')===0;var match=route===current||(route==='/epub'&&current==='/epub-viewer.html')||(plugin&&(current==='/plugins'||current.indexOf('/plugin/')===0));link.classList.toggle('active',match);if(match)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current')})})();</script>";
  if (page.indexOf("<body>") >= 0) {
    page.replace("<body>", "<body>" + rail);
  } else if (page.indexOf("<div class=container>") >= 0) {
    page.replace("<div class=container>", rail + "<div class=container>");
  } else if (page.indexOf("<div class=\"container\">") >= 0) {
    page.replace("<div class=\"container\">", rail + "<div class=\"container\">");
  } else if (page.indexOf("</html>") >= 0) {
    page.replace("</html>", rail + "</html>");
  }

  // The EPUB page uses the same toolbar for search and the grid/list control.
  // It is injected here so plugin pages and the older HTML assets remain compatible.
  if (page.indexOf("id=file-table") >= 0 && page.indexOf("id=inx-book-toolbar") < 0) {
    const String toolbar = "<div class=inx-book-toolbar id=inx-book-toolbar><input class=inx-book-search id=inx-book-search type=search placeholder=\"Search books\" aria-label=\"Search books\"><button class=inx-view-toggle id=inx-list-view-btn type=button title=List view aria-label=List view>☷</button><button class=inx-view-toggle id=inx-grid-view-btn type=button title=Grid view aria-label=Grid view>▦</button></div>";
    page.replace("<div id=file-table>", toolbar + "<div id=file-table>");
  }

  if (page.indexOf("id=file-table") >= 0) {
    const char* epubGridFinalStyle = R"rawliteral(<style id="inx-epub-grid-final-style">.inx-grid .folder-open .inx-folder-stack,.inx-grid .folder-open .inx-folder-cover,.inx-grid .book-open .inx-cover{width:80%!important;max-width:80%!important;height:auto!important;min-height:0!important;aspect-ratio:5/4!important;margin:0!important}.inx-grid .book-open .inx-cover{position:relative;display:block}.inx-grid .book-open .inx-cover img{position:absolute;inset:0;width:100%!important;height:100%!important;object-fit:contain!important;object-position:left center!important}.inx-grid .book-open .inx-cover,.inx-grid .folder-open .inx-cover,.inx-folder-stack,.inx-folder-stack-card,.inx-folder-stack-card img{border-radius:0!important}.inx-cover.has-thumbnail,.inx-folder-stack.has-thumbnail{background:transparent!important;box-shadow:none!important}.inx-cover.has-thumbnail img,.inx-folder-stack.has-thumbnail .inx-folder-stack-card,.inx-folder-stack.has-thumbnail .inx-folder-stack-card img{background:transparent!important;box-shadow:none!important}</style>)rawliteral";
    page.replace("</html>", String(epubGridFinalStyle) + "</html>");
    const char* epubGridSizingStyle = R"rawliteral(<style id="inx-epub-grid-sizing-style">.file-list.inx-grid{grid-template-columns:repeat(7,minmax(0,1fr));justify-content:start}.inx-grid .book-card,.inx-grid .folder-card{max-width:288px}.inx-grid .inx-card-actions{justify-content:flex-end}.inx-grid .folder-open .inx-folder-cover,.inx-grid .book-open .inx-cover{width:100%!important;max-width:100%!important}</style>)rawliteral";
    page.replace("</html>", String(epubGridSizingStyle) + "</html>");
    const char* epubGridVisualStyle = R"rawliteral(<style id="inx-epub-grid-visual-style">.inx-grid .folder-open .inx-folder-stack,.inx-grid .folder-open .inx-folder-cover,.inx-grid .book-open .inx-cover{width:100%!important;max-width:100%!important;aspect-ratio:2/3!important}.inx-grid .inx-grid-delete{position:absolute;z-index:4;top:38px;right:7px;width:30px;height:30px;background:rgba(255,255,255,.94);border:1px solid rgba(255,255,255,.9);box-shadow:0 2px 6px rgba(30,34,38,.18);border-radius:4px!important}.inx-folder-stack.has-thumbnail .folder-stack-1,.inx-folder-stack.has-thumbnail .folder-stack-2{background:#d9dcde!important;box-shadow:none!important}.inx-folder-stack-card img{background:transparent!important}</style>)rawliteral";
    page.replace("</html>", String(epubGridVisualStyle) + "</html>");
  }

  return page;
}

void copySettingString(char* dest, size_t destSize, const char* value) {
  if (destSize == 0) {
    return;
  }
  if (value == nullptr) {
    value = "";
  }
  strncpy(dest, value, destSize - 1);
  dest[destSize - 1] = '\0';
}

String escapeJsonString(const String& input) {
  String out;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input.charAt(i);
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

bool ensureDeviceIdentityDir() {
  if (!SdMan.exists("/.system")) {
    SdMan.mkdir("/.system");
  }
  if (!SdMan.exists(DEVICE_IDENTITY_DIR)) {
    return SdMan.mkdir(DEVICE_IDENTITY_DIR);
  }
  return true;
}

bool readDeviceIdentity(String& name, String& link, String& label, String& tmpl) {
  name = "";
  link = "";
  label = "";
  tmpl = "photo";
  if (!SdMan.exists(DEVICE_IDENTITY_JSON)) {
    return false;
  }

  FsFile file = SdMan.open(DEVICE_IDENTITY_JSON, O_READ);
  if (!file) {
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    return false;
  }

  name = doc["name"] | "";
  link = doc["link"] | "";
  label = doc["label"] | "";
  tmpl = doc["template"] | "photo";
  return true;
}

bool writeDeviceIdentity(const String& name, const String& link, const String& label, const String& tmpl) {
  if (!ensureDeviceIdentityDir()) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForWrite("DID", DEVICE_IDENTITY_JSON, file)) {
    return false;
  }

  JsonDocument doc;
  doc["name"] = name;
  doc["link"] = link;
  doc["label"] = label;
  doc["template"] = tmpl;
  const bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

void sendIdentityImage(WebServer* server, const char* path, const char* contentType) {
  if (!SdMan.exists(path)) {
    server->send(404, "text/plain", "Image not found");
    return;
  }

  FsFile file = SdMan.open(path, O_READ);
  if (!file) {
    server->send(500, "text/plain", "Failed to open image");
    return;
  }

  server->setContentLength(file.size());
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, contentType, "");
  WiFiClient client = server->client();
  client.write(file);
  file.close();
}

void clearEpubCacheIfNeeded(const String& filePath) {
#ifndef INX_SIMULATOR_WEB_ONLY
  if (StringUtils::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.metadata").clearCache();
    INX_SERIAL.printf("[%lu] [WEB] Cleared epub cache for: %s\n", millis(), filePath.c_str());
  }
#else
  (void)filePath;
#endif
}

bool clockSettingsAvailable() {
#ifndef INX_SIMULATOR_WEB_ONLY
  return true;
#else
  return false;
#endif
}

#ifndef INX_SIMULATOR_WEB_ONLY
String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s.charAt(i);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

#endif
}

LocalServer::LocalServer() {}

LocalServer::~LocalServer() { stop(); }

void LocalServer::begin() {
  if (running) {
    INX_SERIAL.printf("[%lu] [WEB] Web server already running\n", millis());
    return;
  }

  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);

  if (!isStaConnected && !isInApMode) {
    INX_SERIAL.printf("[%lu] [WEB] Cannot start webserver - no valid network (mode=%d, status=%d)\n", millis(), wifiMode,
                  WiFi.status());
    return;
  }

  apMode = isInApMode;

  INX_SERIAL.printf("[%lu] [WEB] Network mode: %s\n", millis(), apMode ? "AP" : "STA");

  INX_SERIAL.printf("[%lu] [WEB] Creating web server on port %d...\n", millis(), port);
  server.reset(new WebServer(port));

  WiFi.setSleep(false);

  if (!server) {
    INX_SERIAL.printf("[%lu] [WEB] Failed to create WebServer!\n", millis());
    return;
  }

  INX_SERIAL.printf("[%lu] [WEB] Setting up routes...\n", millis());
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/plugins", HTTP_GET, [this] { handlePluginsPage(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/epub", HTTP_GET, [this] { handleEpubPage(); });
  server->on(UriGlob("/plugin-asset/*"), HTTP_GET, [this] { handlePluginAsset(); });
  server->on(UriGlob("/plugin/*"), HTTP_GET, [this] { handlePluginPage(); });
  server->on("/font-manager", HTTP_GET, [this] { handleFontManagerPage(); });
  server->on("/language-manager", HTTP_GET, [this] { handleLanguageManagerPage(); });
  server->on("/js/inx_font_pack.js", HTTP_GET, [this] { handleInxFontPackJs(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJsZipMinJs(); });
  server->on("/js/qr_creator_logo.min.js", HTTP_GET, [this] { handleQrCreatorLogoJs(); });
  server->on("/js/epub_page.js", HTTP_GET, [this] { handleEpubPageJs(); });
  server->on("/js/files_page.js", HTTP_GET, [this] { handleFilesPageJs(); });
  server->on(UriGlob("/js/plugin/*"), HTTP_GET, [this] { handlePluginPageJs(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/recent", HTTP_GET, [this] { handleRecentBooksData(); });
  server->on("/api/recent", HTTP_DELETE, [this] { handleRecentBookDelete(); });
  server->on("/api/dashboard-stats", HTTP_GET, [this] { handleDashboardStats(); });
  server->on("/api/device-identity", HTTP_GET, [this] { handleDeviceIdentityGet(); });
  server->on("/api/device-identity", HTTP_POST, [this] { handleDeviceIdentityPost(); });
  server->on("/api/device-identity/photo", HTTP_GET, [this] { handleDeviceIdentityPhoto(); });
  server->on("/api/device-identity/card", HTTP_GET, [this] { handleDeviceIdentityCardImage(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/api/library-index", HTTP_GET, [this] { handleLibraryIndexData(); });
  server->on("/api/library-index/refresh", HTTP_POST, [this] { handleLibraryIndexRefresh(); });
  server->on("/api/library-index/status", HTTP_GET, [this] { handleLibraryIndexStatus(); });
  server->on(UriGlob("/api/plugin/*"), HTTP_GET, [this] { handlePluginApi(); });
  server->on(UriGlob("/api/plugin/*"), HTTP_POST, [this] { handlePluginApi(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });

  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  server->on("/delete", HTTP_POST, [this] { handleDelete(); });
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleSettingsGet(); });
  server->on("/api/settings", HTTP_POST, [this] { handleSettingsUpdate(); });
  server->on("/api/language", HTTP_GET, [this] { handleLanguageGet(); });
  server->on("/api/language", HTTP_POST, [this] { handleLanguageUpdate(); });

  server->on("/api/wifi", HTTP_GET, [this] { handleWifiGet(); });
  server->on("/api/wifi", HTTP_POST, [this] { handleWifiPost(); });
  server->on("/api/wifi/*", HTTP_DELETE, [this] { handleWifiDelete(); });
  server->on("/api/koreader", HTTP_GET, [this] { handleKOReaderGet(); });
  server->on("/api/koreader", HTTP_POST, [this] { handleKOReaderPost(); });
#ifndef INX_SIMULATOR_WEB_ONLY
  server->on("/api/gemini", HTTP_GET, [this] { handleGeminiGet(); });
  server->on("/api/gemini", HTTP_POST, [this] { handleGeminiPost(); });
  server->on("/api/gemini", HTTP_DELETE, [this] { handleGeminiDelete(); });
#endif

#ifndef INX_SIMULATOR_WEB_ONLY
  server->on("/api/opds", HTTP_GET, [this] { handleOpdsGet(); });
  server->on("/api/opds", HTTP_POST, [this] { handleOpdsPost(); });
  server->on("/api/opds/*", HTTP_DELETE, [this] { handleOpdsDelete(); });
#endif

  server->on("/api/fonts/rescan", HTTP_POST, [this] { handleFontsRescan(); });

  server->onNotFound([this] { handleNotFound(); });
  INX_SERIAL.printf("✓ jszip.min.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(JSZIP_MIN_JS) - 1));
  INX_SERIAL.printf("✓ epub_page.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(EPUB_PAGE_JS) - 1));
  INX_SERIAL.printf("✓ files_page.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(FILES_PAGE_JS) - 1));
  INX_SERIAL.printf("✓ inx_font_pack.js from firmware flash (%u bytes)\n",
                static_cast<unsigned>(sizeof(INX_FONT_PACK_JS) - 1));

  server->begin();

  INX_SERIAL.printf("[%lu] [WEB] Starting WebSocket server on port %d...\n", millis(), wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<LocalServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  INX_SERIAL.printf("[%lu] [WEB] WebSocket server started\n", millis());

  udpActive = udp.begin(LOCAL_UDP_PORT);
  INX_SERIAL.printf("[%lu] [WEB] Discovery UDP %s on port %d\n", millis(), udpActive ? "enabled" : "failed",
                LOCAL_UDP_PORT);

  running = true;

  INX_SERIAL.printf("[%lu] [WEB] Web server started on port %d\n", millis(), port);

  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  INX_SERIAL.printf("[%lu] [WEB] Access at http://%s/\n", millis(), ipAddr.c_str());
  INX_SERIAL.printf("[%lu] [WEB] WebSocket at ws://%s:%d/\n", millis(), ipAddr.c_str(), wsPort);
}

void LocalServer::stop() {
  if (!running || !server) {
    INX_SERIAL.printf("[%lu] [WEB] stop() called but already stopped (running=%d, server=%p)\n", millis(), running,
                  server.get());
    return;
  }

  INX_SERIAL.printf("[%lu] [WEB] STOP INITIATED - setting running=false first\n", millis());
  running = false;

  if (wsUploadInProgress && wsUploadFile) {
    wsUploadFile.close();
    wsUploadInProgress = false;
  }

  if (wsServer) {
    INX_SERIAL.printf("[%lu] [WEB] Stopping WebSocket server...\n", millis());
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    INX_SERIAL.printf("[%lu] [WEB] WebSocket server stopped\n", millis());
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  delay(20);

  server->stop();

  delay(10);

  server.reset();
  INX_SERIAL.printf("[%lu] [WEB] Web server stopped and deleted\n", millis());
}

void LocalServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  if (!running) {
    return;
  }

  if (!server) {
    INX_SERIAL.printf("[%lu] [WEB] WARNING: handleClient called with null server!\n", millis());
    return;
  }

  if (millis() - lastDebugPrint > 10000) {
    INX_SERIAL.printf("[%lu] [WEB] handleClient active, server running on port %d\n", millis(), port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  if (wsServer) {
    wsServer->loop();
  }

  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

LocalServer::WsUploadStatus LocalServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

void LocalServer::handleRoot() const {
  server->send(200, "text/html", addLanguageManagerNavLink(HomePageHtml, "/"));
  INX_SERIAL.printf("[%lu] [WEB] Served root page\n", millis());
}

void LocalServer::handlePluginsPage() const {
  std::vector<PluginManager::WebLink> links;
  PluginManager::listWebPlugins(links);

  String page = R"rawliteral(<!doctype html><html lang="en"><meta charset="UTF-8"><meta content="width=device-width,initial-scale=1,viewport-fit=cover" name="viewport"><title>Inx — Plugins</title><style>
*{box-sizing:border-box}.container{max-width:1180px;margin:0 auto;padding:34px 28px 48px}.header,.nav-links{display:none}.plugin-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:12px}.plugin-card{display:flex;align-items:center;gap:13px;min-height:76px;padding:13px;text-decoration:none;color:#22272b;border:1px solid #d9dde0;border-radius:5px;background:#fff;transition:border-color .15s,box-shadow .15s,transform .15s}.plugin-card:hover{border-color:#8f979c;box-shadow:0 5px 12px rgba(22,28,32,.1);transform:translateY(-1px)}.plugin-card img,.plugin-card svg{width:38px;height:38px;flex:0 0 38px;object-fit:contain;fill:none;stroke:#697278;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}.plugin-card-copy{min-width:0}.plugin-card-copy strong{display:block;overflow:hidden;font-size:14px;text-overflow:ellipsis;white-space:nowrap}.plugin-card-copy small{display:block;margin-top:4px;color:#737a7f;font-size:12px}.plugin-empty{padding:54px 20px;text-align:center;color:#777f84;font-size:15px}@media(max-width:760px){.container{padding:20px 16px 32px}.plugin-grid{grid-template-columns:1fr}}
</style><div class="container"><div class="plugin-grid">)rawliteral";

  if (links.empty()) {
    page += "<div class=plugin-empty>No plugins installed.</div>";
  } else {
    for (const PluginManager::WebLink& link : links) {
      page += "<a class=plugin-card href=\"";
      page += link.path.c_str();
      page += "\">";
      if (!link.icon.empty()) {
        page += "<img src=\"/plugin-asset/";
        page += link.id.c_str();
        page += "/";
        page += link.icon.c_str();
        page += "\" alt=\"\">";
      } else {
        page += "<svg viewBox=\"0 0 24 24\"><path d=\"M19 13a2 2 0 1 0 0-4h-1V5a2 2 0 0 0-2-2h-4v1a2 2 0 1 1-4 0V3H6a2 2 0 0 0-2 2v4h1a2 2 0 1 1 0 4H4v4a2 2 0 0 0 2 2h4v-1a2 2 0 1 1 4 0v1h4a2 2 0 0 0 2-2v-4Z\"/></svg>";
      }
      page += "<span class=plugin-card-copy><strong>";
      page += escapeHtml(link.label);
      page += "</strong><small>Open plugin</small></span></a>";
    }
  }

  page += "</div></div></html>";
  server->send(200, "text/html; charset=utf-8", addLanguageManagerNavLink(page.c_str(), "/plugins"));
  INX_SERIAL.printf("[%lu] [WEB] Served plugins page (%u plugins)\n", millis(), static_cast<unsigned>(links.size()));
}

void LocalServer::handleNotFound() const {
  // Web plugins declare their own routes in manifest.json. Core routes are
  // registered statically, so resolve manifest-declared plugin paths here
  // before returning the generic 404 page.
  PluginManager::WebLink plugin;
  if (findWebPluginForUri(server->uri(), plugin)) {
    handlePluginPage();
    return;
  }
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void LocalServer::handleStatus() const {
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = INX_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["device"] = BoardConfig::ACTIVE.name;
  doc["displayWidth"] = BoardConfig::ACTIVE.displayWidth;
  doc["displayHeight"] = BoardConfig::ACTIVE.displayHeight;
  doc["screenWidth"] = 480;
  doc["screenHeight"] = 800;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleDashboardStats() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(200, "application/json", "{\"reading\":0,\"finished\":0,\"favorites\":0}");
#else
  const std::vector<BookState::Book> books = BOOK_STATE.getAllBooks();
  size_t reading = 0;
  size_t finished = 0;
  size_t favorites = 0;
  for (const BookState::Book& book : books) {
    reading += book.isReading ? 1u : 0u;
    finished += book.isFinished ? 1u : 0u;
    favorites += book.isFavorite ? 1u : 0u;
  }

  JsonDocument doc;
  doc["reading"] = reading;
  doc["finished"] = finished;
  doc["favorites"] = favorites;
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
#endif
}

void LocalServer::handleRecentBookDelete() const {
  String path = server->arg("path");
  if (path.isEmpty() && server->hasArg("plain")) {
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
      path = doc["path"] | "";
    }
  }

  if (path.isEmpty()) {
    server->send(400, "text/plain", "Missing recent book path");
    return;
  }

  RECENT_BOOKS.loadFromFile();
  RECENT_BOOKS.removeBook(path.c_str());
  server->send(200, "application/json", "{\"ok\":true}");
}

void LocalServer::handleRecentBooksData() const {
  RECENT_BOOKS.loadFromFile();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool first = true;
  const char* coverNames[] = {"thumb.jpg", "thumb.png", "thumb.bmp", "cover.jpg", "cover.png", "cover.bmp"};
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path.empty()) {
      continue;
    }

    std::string title = book.title;
    if (title.empty()) {
      const size_t slash = book.path.find_last_of('/');
      title = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
      const size_t dot = title.find_last_of('.');
      if (dot != std::string::npos) {
        title.resize(dot);
      }
    }

    const std::string cachePath = book.cachePath.empty() ? epubCachePathForBookPath(book.path) : book.cachePath;
    std::string coverPath;
    for (const char* coverName : coverNames) {
      const std::string candidate = cachePath + "/" + coverName;
      if (SdMan.exists(candidate.c_str())) {
        coverPath = candidate;
        break;
      }
    }

    String row = "{\"path\":\"";
    row += jsonEscape(book.path.c_str());
    row += "\",\"title\":\"";
    row += jsonEscape(title.c_str());
    row += "\",\"author\":\"";
    row += jsonEscape(book.author.c_str());
    row += "\",\"progress\":";
    if (book.progress >= 0.0f) {
      row += String(book.progress, 4);
    } else {
      row += "null";
    }
    row += ",\"coverUrl\":";
    if (coverPath.empty()) {
      row += "null";
    } else {
      String coverUrl = "/download?path=";
      coverUrl += coverPath.c_str();
      coverUrl += "&inline=1";
      row += "\"";
      row += jsonEscape(coverUrl);
      row += "\"";
    }
    row += "}";

    if (!first) {
      server->sendContent(",");
    }
    first = false;
    server->sendContent(row);
  }

  server->sendContent("]");
}

void LocalServer::handleDeviceIdentityGet() const {
  String name;
  String link;
  String label;
  String tmpl;
  readDeviceIdentity(name, link, label, tmpl);

  String json = "{\"ok\":true";
  json += ",\"name\":\"" + escapeJsonString(name) + "\"";
  json += ",\"link\":\"" + escapeJsonString(link) + "\"";
  json += ",\"label\":\"" + escapeJsonString(label) + "\"";
  json += ",\"template\":\"" + escapeJsonString(tmpl) + "\"";
  json += ",\"hasPhoto\":";
  json += SdMan.exists(DEVICE_IDENTITY_PHOTO) ? "true" : "false";
  json += ",\"hasCard\":";
  json += SdMan.exists(DEVICE_IDENTITY_CARD) ? "true" : "false";
  json += "}";
  server->send(200, "application/json", json);
}

void LocalServer::handleDeviceIdentityPost() const {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server->arg("plain"));
  if (error) {
    server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  String name = doc["name"] | "";
  String link = doc["link"] | "";
  String label = doc["label"] | "";
  String tmpl = doc["template"] | "photo";
  name.trim();
  link.trim();
  label.trim();
  if (name.length() > 64) {
    name = name.substring(0, 64);
  }
  if (link.length() > 180) {
    link = link.substring(0, 180);
  }
  if (label.length() > 48) {
    label = label.substring(0, 48);
  }
  if (tmpl != "minimal") {
    tmpl = "photo";
  }

  if (!writeDeviceIdentity(name, link, label, tmpl)) {
    server->send(500, "application/json", "{\"ok\":false,\"error\":\"Could not save identity\"}");
    return;
  }

  server->send(200, "application/json", "{\"ok\":true}");
}

void LocalServer::handleDeviceIdentityPhoto() const {
  sendIdentityImage(server.get(), DEVICE_IDENTITY_PHOTO, "image/png");
}

void LocalServer::handleDeviceIdentityCardImage() const {
  sendIdentityImage(server.get(), DEVICE_IDENTITY_CARD, "image/jpeg");
}

void LocalServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  FsFile root = SdMan.open(path);
  if (!root) {
    INX_SERIAL.printf("[%lu] [WEB] Failed to open directory: %s\n", millis(), path);
    return;
  }

  if (!root.isDirectory()) {
    INX_SERIAL.printf("[%lu] [WEB] Not a directory: %s\n", millis(), path);
    root.close();
    return;
  }

  INX_SERIAL.printf("[%lu] [WEB] Scanning files in: %s\n", millis(), path);

  FsFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    bool shouldHide = fileName.startsWith(".");

    if (!shouldHide) {
      for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
        if (fileName.equals(HIDDEN_ITEMS[i])) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();
    esp_task_wdt_reset();
    file = root.openNextFile();
  }
  root.close();
}

bool LocalServer::isEpubFile(const String& filename) const {
  std::string lower = filename.c_str();
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".epub") == 0;
}

void LocalServer::handleFileList() const { server->send(200, "text/html", addLanguageManagerNavLink(FilesPageHtml, "/files")); }

void LocalServer::handleEpubPage() const {
  server->send(200, "text/html; charset=utf-8", addLanguageManagerNavLink(EpubPageHtml, "/epub"));
}

void LocalServer::handlePluginPage() const {
  PluginManager::WebLink plugin;
  if (!findWebPluginForUri(server->uri(), plugin)) {
    server->send(404, "text/plain", "No plugin web page is installed");
    return;
  }
  std::string html;
  std::string error;
  if (!PluginManager::readFile(plugin.id.c_str(), plugin.page.c_str(), html, 64 * 1024, error)) {
    server->send(404, "text/plain", error.c_str());
    return;
  }
  server->send(200, "text/html; charset=utf-8", addLanguageManagerNavLink(html.c_str(), server->uri().c_str()));
}

void LocalServer::handleFontManagerPage() const {
  server->send(200, "text/html", addLanguageManagerNavLink(FontManagerPageHtml, "/font-manager"));
}

void LocalServer::handleLanguageManagerPage() const {
  server->send(200, "text/html", addLanguageManagerNavLink(LanguageManagerPageHtml, "/language-manager"));
}

void LocalServer::handleInxFontPackJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), INX_FONT_PACK_JS, sizeof(INX_FONT_PACK_JS) - 1);
}

void LocalServer::handleJsZipMinJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), JSZIP_MIN_JS, sizeof(JSZIP_MIN_JS) - 1);
}

void LocalServer::handleQrCreatorLogoJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), QR_CREATOR_LOGO_JS, sizeof(QR_CREATOR_LOGO_JS) - 1);
}

void LocalServer::handleEpubPageJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), EPUB_PAGE_JS, sizeof(EPUB_PAGE_JS) - 1);
}

void LocalServer::handleFilesPageJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), FILES_PAGE_JS, sizeof(FILES_PAGE_JS) - 1);
}

void LocalServer::handlePluginPageJs() const {
  String requestUri = server->uri();
  if (requestUri.startsWith("/js/plugin/")) requestUri = "/plugin/" + requestUri.substring(11);

  PluginManager::WebLink plugin;
  if (!findWebPluginForUri(requestUri, plugin)) {
    server->send(404, "text/plain", "No plugin web page is installed");
    return;
  }
  std::string script;
  std::string error;
  if (!PluginManager::readFile(plugin.id.c_str(), plugin.script.c_str(), script, 32 * 1024, error)) {
    server->send(404, "text/plain", error.c_str());
    return;
  }
  server->send(200, "text/javascript; charset=utf-8", script.c_str());
}

void LocalServer::handlePluginAsset() const {
  const String prefix = "/plugin-asset/";
  const String uri = server->uri();
  if (!uri.startsWith(prefix)) {
    server->send(404, "text/plain", "Plugin asset not found");
    return;
  }

  const String route = uri.substring(prefix.length());
  const int separator = route.indexOf('/');
  if (separator <= 0 || separator >= route.length() - 1) {
    server->send(400, "text/plain", "Invalid plugin asset path");
    return;
  }

  const String pluginId = route.substring(0, separator);
  const String filename = route.substring(separator + 1);
  std::vector<PluginManager::WebLink> links;
  bool allowed = false;
  if (PluginManager::listWebPlugins(links)) {
    for (const PluginManager::WebLink& link : links) {
      if (pluginId == link.id.c_str() && filename == link.icon.c_str()) {
        allowed = true;
        break;
      }
    }
  }
  if (!allowed) {
    server->send(404, "text/plain", "Plugin asset not found");
    return;
  }

  std::string contents;
  std::string error;
  if (!PluginManager::readFile(pluginId.c_str(), filename.c_str(), contents, 64 * 1024, error)) {
    server->send(404, "text/plain", error.c_str());
    return;
  }

  const char* contentType = "application/octet-stream";
  if (filename.endsWith(".png")) contentType = "image/png";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (filename.endsWith(".svg")) contentType = "image/svg+xml";
  else if (filename.endsWith(".webp")) contentType = "image/webp";

  server->setContentLength(contents.size());
  server->send(200, contentType, "");
  server->sendContent(contents.c_str(), contents.size());
}

void LocalServer::handleFileListData() const {
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = server->arg("path");

    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }

    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst, currentPath](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;
    if (info.isDirectory) {
      String folderPath = currentPath;
      if (folderPath == "/") {
        folderPath += info.name;
      } else {
        folderPath += "/";
        folderPath += info.name;
      }
      std::vector<String> folderCoverUrls;
      auto addFolderCover = [this, &folderCoverUrls](const String& bookPath) {
        if (folderCoverUrls.size() >= 3) return;
        const std::string cachePath = epubCachePathForBookPath(bookPath.c_str());
        const char* coverNames[] = {"cover.jpg", "thumb.jpg", "cover.bmp", "thumb.png", "thumb.bmp"};
        for (const char* coverName : coverNames) {
          const std::string coverPath = cachePath + "/" + coverName;
          if (!SdMan.exists(coverPath.c_str())) continue;
          String coverUrl = "/download?path=";
          coverUrl += coverPath.c_str();
          coverUrl += "&inline=1";
          folderCoverUrls.push_back(coverUrl);
          break;
        }
      };

      // Match the device library stack: prefer books directly in the folder,
      // then fall back to books in nested folders when there are no direct covers.
      uint8_t directChecked = 0;
      scanFiles(folderPath.c_str(), [&](const FileInfo& child) {
        if (directChecked >= 32 || !child.isEpub) return;
        ++directChecked;
        String bookPath = folderPath + "/" + child.name;
        addFolderCover(bookPath);
      });

      if (folderCoverUrls.empty()) {
        uint8_t nestedChecked = 0;
        std::function<void(const String&)> scanNested = [&](const String& directory) {
          if (folderCoverUrls.size() >= 3 || nestedChecked >= 64) return;
          scanFiles(directory.c_str(), [&](const FileInfo& child) {
            if (folderCoverUrls.size() >= 3 || nestedChecked >= 64) return;
            ++nestedChecked;
            const String childPath = directory + "/" + child.name;
            if (child.isEpub) {
              addFolderCover(childPath);
            } else if (child.isDirectory) {
              scanNested(childPath);
            }
          });
        };
        scanNested(folderPath);
      }

      for (size_t index = 0; index < folderCoverUrls.size() && index < 3; ++index) {
        doc["coverUrls"][index] = folderCoverUrls[index];
      }
    } else if (info.isEpub) {
      String bookPath = currentPath;
      if (bookPath == "/") {
        bookPath += info.name;
      } else {
        bookPath += "/";
        bookPath += info.name;
      }
      const std::string cachePath = epubCachePathForBookPath(bookPath.c_str());
      const char* coverNames[] = {"cover.jpg", "thumb.jpg", "cover.bmp", "thumb.png", "thumb.bmp"};
      for (const char* coverName : coverNames) {
        const std::string coverPath = cachePath + "/" + coverName;
        if (!SdMan.exists(coverPath.c_str())) continue;
        String coverUrl = "/download?path=";
        coverUrl += coverPath.c_str();
        coverUrl += "&inline=1";
        doc["coverUrl"] = coverUrl;
        break;
      }
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      INX_SERIAL.printf("[%lu] [WEB] Skipping file entry with oversized JSON for name: %s\n", millis(), info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");

  server->sendContent("");
  INX_SERIAL.printf("[%lu] [WEB] Served file listing page for path: %s\n", millis(), currentPath.c_str());
}

void LocalServer::handleLibraryIndexData() const {
#ifndef INX_SIMULATOR_WEB_ONLY
  if (!LibraryIndex::hasIndex()) {
    server->send(404, "text/plain", "Library index unavailable");
    return;
  }

  std::vector<LibraryIndex::Book> entries;
  if (!LibraryIndex::search("", entries, LibraryIndex::all)) {
    server->send(500, "text/plain", "Could not read library index");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  JsonDocument document;
  bool first = true;
  for (const LibraryIndex::Book& entry : entries) {
    if (entry.type != LibraryIndex::Book::Type::BOOK) continue;
    document.clear();
    document["path"] = entry.path;
    document["name"] = entry.title.empty() ? entry.path : entry.title;
    document["title"] = entry.title;
    document["author"] = entry.author;

    const std::string cachePath = epubCachePathForBookPath(entry.path.c_str());
    const char* coverNames[] = {"cover.jpg", "thumb.jpg", "cover.bmp", "thumb.png", "thumb.bmp"};
    for (const char* coverName : coverNames) {
      const std::string coverPath = cachePath + "/" + coverName;
      if (!SdMan.exists(coverPath.c_str())) continue;
      String coverUrl = "/download?path=";
      coverUrl += coverPath.c_str();
      coverUrl += "&inline=1";
      document["coverUrl"] = coverUrl;
      break;
    }

    String output;
    serializeJson(document, output);
    if (!first) server->sendContent(",");
    first = false;
    server->sendContent(output);
  }
  server->sendContent("]");
  server->sendContent("");
#else
  server->send(404, "text/plain", "Library index unavailable");
#endif
}

void LocalServer::handleLibraryIndexRefresh() const {
#ifndef INX_SIMULATOR_WEB_ONLY
  if (LibraryIndexRefresh::isRunning()) {
    server->send(409, "text/plain", "Library index refresh already running");
    return;
  }
  LibraryIndexRefresh::start(render, currentActivity);
  server->send(202, "text/plain", "Library index refresh started");
#else
  server->send(503, "text/plain", "Library index refresh unavailable");
#endif
}

void LocalServer::handleLibraryIndexStatus() const {
  JsonDocument document;
#ifndef INX_SIMULATOR_WEB_ONLY
  document["available"] = LibraryIndex::hasIndex();
  document["refreshing"] = LibraryIndexRefresh::isRunning();
#else
  document["available"] = false;
  document["refreshing"] = false;
#endif
  String output;
  serializeJson(document, output);
  server->send(200, "application/json", output);
}

void LocalServer::handlePluginApi() const {
  const String prefix = "/api/plugin/";
  const String uri = server->uri();
  if (!uri.startsWith(prefix)) {
    server->send(404, "text/plain", "Plugin endpoint not found");
    return;
  }
  const String route = uri.substring(prefix.length());
  const int separator = route.indexOf('/');
  if (separator <= 0 || separator >= route.length() - 1) {
    server->send(400, "text/plain", "Invalid plugin endpoint");
    return;
  }
  const String pluginId = route.substring(0, separator);
  const String function = route.substring(separator + 1);
  JsonDocument arguments;
  if (server->hasArg("plain") && !server->arg("plain").isEmpty()) {
    if (deserializeJson(arguments, server->arg("plain")) != DeserializationError::Ok ||
        !arguments.is<JsonObject>()) {
      server->send(400, "text/plain", "Plugin arguments must be a JSON object");
      return;
    }
  } else {
    JsonObject query = arguments.to<JsonObject>();
    for (int index = 0; index < server->args(); ++index) {
      const String name = server->argName(index);
      if (name == "plain") continue;
      query[name.c_str()] = server->arg(index);
    }
  }
  String argumentJson;
  serializeJson(arguments, argumentJson);
  std::string output;
  std::string error;
  if (!PluginManager::invokeStringJson(pluginId.c_str(), function.c_str(), argumentJson.c_str(), output, error)) {
    server->send(404, "text/plain", error.c_str());
    return;
  }
  // Plugin functions own their response format. Keep the host route generic;
  // download behavior and filenames belong to the plugin's web UI.
  server->send(200, "text/plain; charset=utf-8", output.c_str());
}

void LocalServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  if (!SdMan.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file = SdMan.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  } else if (itemPath.endsWith(".jpg") || itemPath.endsWith(".jpeg") || itemPath.endsWith(".JPG") ||
             itemPath.endsWith(".JPEG")) {
    contentType = "image/jpeg";
  } else if (itemPath.endsWith(".png") || itemPath.endsWith(".PNG")) {
    contentType = "image/png";
  } else if (itemPath.endsWith(".bmp") || itemPath.endsWith(".BMP")) {
    contentType = "image/bmp";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  const bool inlineView = server->hasArg("inline") && server->arg("inline") == "1";
  server->sendHeader("Content-Disposition",
                     String(inlineView ? "inline" : "attachment") + "; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  WiFiClient client = server->client();
  client.write(file);
  file.close();
}

static FsFile uploadFile;
static String uploadFileName;
static String uploadPath = "/";
static size_t uploadSize = 0;
static bool uploadSuccess = false;
static String uploadError = "";

constexpr size_t UPLOAD_BUFFER_SIZE = 4096;
static uint8_t* uploadBuffer = nullptr;
static size_t uploadBufferPos = 0;

static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static void freeUploadBuffer() {
  std::free(uploadBuffer);
  uploadBuffer = nullptr;
  uploadBufferPos = 0;
}

static bool flushUploadBuffer() {
  if (uploadBufferPos > 0 && uploadFile && uploadBuffer) {
    esp_task_wdt_reset();
    const unsigned long writeStart = millis();
    const size_t written = uploadFile.write(uploadBuffer, uploadBufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();

    if (written != uploadBufferPos) {
      INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] Buffer flush failed: expected %d, wrote %d\n", millis(), uploadBufferPos,
                    written);
      uploadBufferPos = 0;
      return false;
    }
    uploadBufferPos = 0;
    yield();
  }
  return true;
}

void LocalServer::handleUpload() const {
  static size_t lastLoggedSize = 0;

  esp_task_wdt_reset();

  if (!running || !server) {
    INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] ERROR: handleUpload called but server not running!\n", millis());
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    esp_task_wdt_reset();

    uploadFileName = upload.filename;
    uploadSize = 0;
    uploadSuccess = false;
    uploadError = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    uploadBufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;
    freeUploadBuffer();

    if (server->hasArg("path")) {
      uploadPath = server->arg("path");

      if (!uploadPath.startsWith("/")) {
        uploadPath = "/" + uploadPath;
      }

      if (uploadPath.length() > 1 && uploadPath.endsWith("/")) {
        uploadPath = uploadPath.substring(0, uploadPath.length() - 1);
      }
    } else {
      uploadPath = "/";
    }

    INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] START: %s to path: %s\n", millis(), uploadFileName.c_str(), uploadPath.c_str());

    String filePath = uploadPath;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += uploadFileName;

    esp_task_wdt_reset();
    if (SdMan.exists(filePath.c_str())) {
      INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] Overwriting existing file: %s\n", millis(), filePath.c_str());
      esp_task_wdt_reset();
      SdMan.remove(filePath.c_str());
    }

    esp_task_wdt_reset();
    if (!SdMan.openFileForWrite("WEB", filePath, uploadFile)) {
      uploadError = "Failed to create file on SD card";
      INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] FAILED to create file: %s\n", millis(), filePath.c_str());
      return;
    }
    esp_task_wdt_reset();

    uploadBuffer = static_cast<uint8_t*>(std::malloc(UPLOAD_BUFFER_SIZE));
    if (!uploadBuffer) {
      uploadError = "Failed to allocate upload buffer";
      uploadFile.close();
      SdMan.remove(filePath.c_str());
      INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] FAILED to allocate %d byte buffer\n", millis(), UPLOAD_BUFFER_SIZE);
      return;
    }

    INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] File created successfully: %s\n", millis(), filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadError.isEmpty()) {
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UPLOAD_BUFFER_SIZE - uploadBufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(uploadBuffer + uploadBufferPos, data, toCopy);
        uploadBufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        if (uploadBufferPos >= UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer()) {
            uploadError = "Failed to write to SD card - disk may be full";
            uploadFile.close();
            return;
          }
        }
      }

      uploadSize += upload.currentSize;

      if (uploadSize - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes\n", millis(), uploadSize,
                      uploadSize / 1024.0, kbps, writeCount);
        lastLoggedSize = uploadSize;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      if (!flushUploadBuffer()) {
        uploadError = "Failed to write final data to SD card";
      }
      uploadFile.close();
      freeUploadBuffer();

      if (uploadError.isEmpty()) {
        uploadSuccess = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)\n", millis(),
                      uploadFileName.c_str(), uploadSize, elapsed, avgKbps);
        INX_SERIAL.printf("[%lu] [WEB] [UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)\n", millis(),
                      writeCount, totalWriteTime, writePercent);

        String filePath = uploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += uploadFileName;
        clearEpubCacheIfNeeded(filePath);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    freeUploadBuffer();
    if (uploadFile) {
      uploadFile.close();

      String filePath = uploadPath;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += uploadFileName;
      SdMan.remove(filePath.c_str());
    }
    uploadError = "Upload aborted";
    INX_SERIAL.printf("[%lu] [WEB] Upload aborted\n", millis());
  }
}

void LocalServer::handleUploadPost() const {
  if (uploadSuccess) {
    server->send(200, "text/plain", "File uploaded successfully: " + uploadFileName);
  } else {
    const String error = uploadError.isEmpty() ? "Unknown error during upload" : uploadError;
    server->send(400, "text/plain", error);
  }
}

void LocalServer::handleCreateFolder() const {
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = server->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  INX_SERIAL.printf("[%lu] [WEB] Creating folder: %s\n", millis(), folderPath.c_str());

  if (SdMan.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  if (SdMan.mkdir(folderPath.c_str())) {
    INX_SERIAL.printf("[%lu] [WEB] Folder created successfully: %s\n", millis(), folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    INX_SERIAL.printf("[%lu] [WEB] Failed to create folder: %s\n", millis(), folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void LocalServer::handleDelete() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  const String itemType = server->hasArg("type") ? server->arg("type") : "file";

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot delete root directory");
    return;
  }

  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  if (itemName.startsWith(".")) {
    INX_SERIAL.printf("[%lu] [WEB] Delete rejected - hidden/system item: %s\n", millis(), itemPath.c_str());
    server->send(403, "text/plain", "Cannot delete system files");
    return;
  }

  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      INX_SERIAL.printf("[%lu] [WEB] Delete rejected - protected item: %s\n", millis(), itemPath.c_str());
      server->send(403, "text/plain", "Cannot delete protected items");
      return;
    }
  }

  if (!SdMan.exists(itemPath.c_str())) {
    INX_SERIAL.printf("[%lu] [WEB] Delete failed - item not found: %s\n", millis(), itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  INX_SERIAL.printf("[%lu] [WEB] Attempting to delete %s: %s\n", millis(), itemType.c_str(), itemPath.c_str());

  bool success = false;

  if (itemType == "folder") {
    FsFile dir = SdMan.open(itemPath.c_str());
    if (dir && dir.isDirectory()) {
      FsFile entry = dir.openNextFile();
      if (entry) {
        entry.close();
        dir.close();
        INX_SERIAL.printf("[%lu] [WEB] Delete failed - folder not empty: %s\n", millis(), itemPath.c_str());
        server->send(400, "text/plain", "Folder is not empty. Delete contents first.");
        return;
      }
      dir.close();
    }
    success = SdMan.rmdir(itemPath.c_str());
  } else {
    success = SdMan.remove(itemPath.c_str());
  }

  if (success) {
    INX_SERIAL.printf("[%lu] [WEB] Successfully deleted: %s\n", millis(), itemPath.c_str());
    server->send(200, "text/plain", "Deleted successfully");
  } else {
    INX_SERIAL.printf("[%lu] [WEB] Failed to delete: %s\n", millis(), itemPath.c_str());
    server->send(500, "text/plain", "Failed to delete item");
  }
}

void LocalServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("destination")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = server->arg("path");
  String destination = server->arg("destination");
  itemPath.trim();
  destination.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot move root directory");
    return;
  }
  if (!itemPath.startsWith("/")) itemPath = "/" + itemPath;
  if (itemPath.length() > 1 && itemPath.endsWith("/")) itemPath.remove(itemPath.length() - 1);
  if (!destination.startsWith("/")) destination = "/" + destination;
  if (destination.length() > 1 && destination.endsWith("/")) destination.remove(destination.length() - 1);

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot move system files");
    return;
  }
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      server->send(403, "text/plain", "Cannot move protected items");
      return;
    }
  }
  if (!SdMan.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile destinationDir = SdMan.open(destination.c_str());
  if (!destinationDir || !destinationDir.isDirectory()) {
    if (destinationDir) destinationDir.close();
    server->send(400, "text/plain", "Destination folder not found");
    return;
  }
  destinationDir.close();

  FsFile item = SdMan.open(itemPath.c_str());
  const bool isDir = item && item.isDirectory();
  if (item) item.close();

  const String sourcePrefix = itemPath + "/";
  if (isDir && (destination == itemPath || destination.startsWith(sourcePrefix))) {
    server->send(400, "text/plain", "Cannot move a folder into itself");
    return;
  }

  const String newPath = destination == "/" ? "/" + itemName : destination + "/" + itemName;
  if (newPath == itemPath) {
    server->send(200, "text/plain", "Already in that folder");
    return;
  }
  if (SdMan.exists(newPath.c_str())) {
    server->send(409, "text/plain", "An item with that name already exists in the destination folder");
    return;
  }

  std::vector<std::pair<std::string, std::string>> epubRenames;
  if (isDir) {
    collectEpubRenames(itemPath.c_str(), newPath.c_str(), epubRenames);
  } else if (isEpubFile(itemName)) {
    epubRenames.emplace_back(itemPath.c_str(), newPath.c_str());
  }

  INX_SERIAL.printf("[%lu] [WEB] Moving %s -> %s\n", millis(), itemPath.c_str(), newPath.c_str());
  if (!SdMan.rename(itemPath.c_str(), newPath.c_str())) {
    server->send(500, "text/plain", "Failed to move item");
    return;
  }

  for (const auto& renamePair : epubRenames) {
    migrateEpubBookState(renamePair.first, renamePair.second);
  }

  server->send(200, "text/plain", "Moved successfully");
}

void LocalServer::collectEpubRenames(const std::string& oldDirPath, const std::string& newDirPath,
                                     std::vector<std::pair<std::string, std::string>>& out) const {
  scanFiles(oldDirPath.c_str(), [&](const FileInfo info) {
    const std::string childOld = oldDirPath + "/" + info.name.c_str();
    const std::string childNew = newDirPath + "/" + info.name.c_str();
    if (info.isDirectory) {
      collectEpubRenames(childOld, childNew, out);
    } else if (info.isEpub) {
      out.emplace_back(childOld, childNew);
    }
  });
}

void LocalServer::migrateEpubBookState(const std::string& oldPath, const std::string& newPath) const {
#ifndef INX_SIMULATOR_WEB_ONLY
  const std::string oldCachePath = "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(oldPath));
  const std::string newCachePath = "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(newPath));

  if (SdMan.exists(oldCachePath.c_str()) && !SdMan.exists(newCachePath.c_str())) {
    SdMan.rename(oldCachePath.c_str(), newCachePath.c_str());
  }

  BOOK_STATE.renamePath(oldPath, newPath);
  RECENT_BOOKS.renamePath(oldPath, newPath, newCachePath);
#else
  (void)oldPath;
  (void)newPath;
#endif
}

void LocalServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or name");
    return;
  }

  String itemPath = server->arg("path");
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot rename root directory");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }
  if (itemPath.length() > 1 && itemPath.endsWith("/")) {
    itemPath = itemPath.substring(0, itemPath.length() - 1);
  }

  if (newName.isEmpty() || newName.indexOf('/') != -1 || newName.indexOf('\\') != -1 || newName == "." ||
      newName == "..") {
    server->send(400, "text/plain", "Invalid name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  if (itemName.startsWith(".")) {
    INX_SERIAL.printf("[%lu] [WEB] Rename rejected - hidden/system item: %s\n", millis(), itemPath.c_str());
    server->send(403, "text/plain", "Cannot rename system files");
    return;
  }

  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      INX_SERIAL.printf("[%lu] [WEB] Rename rejected - protected item: %s\n", millis(), itemPath.c_str());
      server->send(403, "text/plain", "Cannot rename protected items");
      return;
    }
  }

  if (!SdMan.exists(itemPath.c_str())) {
    INX_SERIAL.printf("[%lu] [WEB] Rename failed - item not found: %s\n", millis(), itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  const int lastSlash = itemPath.lastIndexOf('/');
  const String parentPath = lastSlash > 0 ? itemPath.substring(0, lastSlash) : String("");
  const String newPath = parentPath + "/" + newName;

  if (newPath == itemPath) {
    server->send(200, "text/plain", "Renamed successfully");
    return;
  }

  if (strcasecmp(newName.c_str(), itemName.c_str()) != 0 && SdMan.exists(newPath.c_str())) {
    server->send(409, "text/plain", "An item with that name already exists");
    return;
  }

  FsFile item = SdMan.open(itemPath.c_str());
  const bool isDir = item && item.isDirectory();
  if (item) {
    item.close();
  }

  std::vector<std::pair<std::string, std::string>> epubRenames;
  if (isDir) {
    collectEpubRenames(itemPath.c_str(), newPath.c_str(), epubRenames);
  } else if (isEpubFile(itemName)) {
    epubRenames.emplace_back(itemPath.c_str(), newPath.c_str());
  }

  INX_SERIAL.printf("[%lu] [WEB] Renaming %s -> %s\n", millis(), itemPath.c_str(), newPath.c_str());

  if (!SdMan.rename(itemPath.c_str(), newPath.c_str())) {
    INX_SERIAL.printf("[%lu] [WEB] Failed to rename: %s\n", millis(), itemPath.c_str());
    server->send(500, "text/plain", "Failed to rename item");
    return;
  }

  for (const auto& renamePair : epubRenames) {
    migrateEpubBookState(renamePair.first, renamePair.second);
  }

  INX_SERIAL.printf("[%lu] [WEB] Successfully renamed: %s -> %s\n", millis(), itemPath.c_str(), newPath.c_str());
  server->send(200, "text/plain", "Renamed successfully");
}

void LocalServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

void LocalServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      INX_SERIAL.printf("[%lu] [WS] Client %u disconnected\n", millis(), num);

      if (wsUploadInProgress && wsUploadFile) {
        wsUploadFile.close();

        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        SdMan.remove(filePath.c_str());
        INX_SERIAL.printf("[%lu] [WS] Deleted incomplete upload: %s\n", millis(), filePath.c_str());
      }
      wsUploadInProgress = false;
      break;

    case WStype_CONNECTED: {
      INX_SERIAL.printf("[%lu] [WS] Client %u connected\n", millis(), num);
      break;
    }

    case WStype_TEXT: {
      String msg = String((char*)payload);
      INX_SERIAL.printf("[%lu] [WS] Text from client %u: %s\n", millis(), num, msg.c_str());

      if (msg.startsWith("START:")) {
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          wsUploadSize = msg.substring(firstColon + 1, secondColon).toInt();
          wsUploadPath = msg.substring(secondColon + 1);
          wsUploadReceived = 0;
          wsUploadStartTime = millis();

          if (!wsUploadPath.startsWith("/")) wsUploadPath = "/" + wsUploadPath;
          if (wsUploadPath.length() > 1 && wsUploadPath.endsWith("/")) {
            wsUploadPath = wsUploadPath.substring(0, wsUploadPath.length() - 1);
          }

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          INX_SERIAL.printf("[%lu] [WS] Starting upload: %s (%d bytes) to %s\n", millis(), wsUploadFileName.c_str(),
                        wsUploadSize, filePath.c_str());

          esp_task_wdt_reset();
          if (SdMan.exists(filePath.c_str())) {
            SdMan.remove(filePath.c_str());
          }

          esp_task_wdt_reset();
          if (!SdMan.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            return;
          }
          esp_task_wdt_reset();

          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      esp_task_wdt_reset();
      size_t written = wsUploadFile.write(payload, length);
      esp_task_wdt_reset();

      if (written != length) {
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      static size_t lastProgressSent = 0;
      if (wsUploadReceived - lastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        lastProgressSent = wsUploadReceived;
      }

      if (wsUploadReceived >= wsUploadSize) {
        wsUploadFile.close();
        wsUploadInProgress = false;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        INX_SERIAL.printf("[%lu] [WS] Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)\n", millis(),
                      wsUploadFileName.c_str(), wsUploadSize, elapsed, kbps);

        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearEpubCacheIfNeeded(filePath);

        wsServer->sendTXT(num, "DONE");
        lastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

void LocalServer::handleSettingsPage() const {
  server->send(200, "text/html", addLanguageManagerNavLink(SettingsPageHtml, "/settings"));
  INX_SERIAL.printf("[%lu] [WEB] Served settings page\n", millis());
}

void LocalServer::handleSettingsGet() const {
  JsonDocument doc;
  const bool clockAvailable = clockSettingsAvailable();
  const uint8_t sleepScreen = (!clockAvailable && SETTINGS.sleepScreen == SystemSetting::DATETIME)
                                  ? SystemSetting::LIGHT
                                  : SETTINGS.sleepScreen;

  doc["clockAvailable"] = clockAvailable;
  doc["sleepScreen"] = sleepScreen;
  doc["sleepScreenCoverMode"] = SETTINGS.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = SETTINGS.sleepScreenCoverFilter;
  doc["sleepImageQuality"] = SETTINGS.sleepImageQuality;
  doc["sleepScreenCoverGrayscale"] = SETTINGS.sleepImageQuality;
  doc["sleepImageTwoBit"] = SETTINGS.sleepImageQuality != SystemSetting::SLEEP_IMAGE_LOW;
  doc["sleepCustomBmp"] = SETTINGS.sleepCustomBmp;
  if (clockAvailable) {
    doc["sleepClockStyle"] = SETTINGS.sleepClockStyle;
    doc["sleepClockTimeFormat"] = SETTINGS.sleepClockTimeFormat;
    doc["timeZoneQuarterOffset"] = SETTINGS.timeZoneQuarterOffset;
  }
  doc["hideBatteryPercentage"] = SETTINGS.hideBatteryPercentage;
  doc["recentLibraryMode"] = SETTINGS.recentLibraryMode;
  doc["libraryMode"] = SETTINGS.libraryMode;
  doc["frontButtonLayout"] = SETTINGS.frontButtonLayout;
  doc["recentVisibleCount"] = SETTINGS.recentVisibleCount;
  doc["librarySortEnabled"] = SETTINGS.librarySortEnabled;
  doc["libraryShelfEnabled"] = SETTINGS.libraryShelfEnabled;
  doc["librarySortMode"] = SETTINGS.librarySortMode;

  doc["fontFamily"] = READER_SETTINGS.fontFamily;
  doc["fontSize"] = READER_SETTINGS.fontSize;

  doc["lineHeight"] = READER_SETTINGS.lineHeight;
  doc["textSpace"] = READER_SETTINGS.textSpace;
  doc["screenMargin"] = READER_SETTINGS.screenMargin;
  doc["paragraphAlignment"] = READER_SETTINGS.paragraphAlignment;
  doc["paragraphCssIndentEnabled"] = READER_SETTINGS.paragraphCssIndentEnabled;
  doc["extraParagraphSpacing"] = READER_SETTINGS.extraParagraphSpacing;
  doc["orientation"] = READER_SETTINGS.orientation;
  doc["hyphenationEnabled"] = READER_SETTINGS.hyphenationEnabled;
  doc["bionicReadingEnabled"] = READER_SETTINGS.bionicReadingEnabled;

  doc["shakePageTurn"] = SETTINGS.shakePageTurn;
  doc["shakePageTurnSensitivity"] = SETTINGS.shakePageTurnSensitivity;

  doc["textAntiAliasing"] = READER_SETTINGS.textAntiAliasing;
  doc["refreshFrequency"] = READER_SETTINGS.refreshFrequency;
  doc["readerImageGrayscale"] = READER_SETTINGS.readerImageGrayscale;
  doc["readerSmartRefreshOnImages"] = READER_SETTINGS.readerSmartRefreshOnImages;
  doc["statusBar"] = READER_SETTINGS.statusBar;
  doc["statusBarLeft"] = READER_SETTINGS.statusBarLeft;
  doc["statusBarMiddle"] = READER_SETTINGS.statusBarMiddle;
  doc["statusBarRight"] = READER_SETTINGS.statusBarRight;
  doc["statusBarFullStyle"] = READER_SETTINGS.statusBarFullStyle;

  doc["shortPwrBtn"] = SETTINGS.shortPwrBtn;

  doc["sleepTimeout"] = SETTINGS.sleepTimeout;
  doc["useLibraryIndex"] = SETTINGS.useLibraryIndex;
  doc["bootSetting"] = SETTINGS.bootSetting;

  doc["refreshOnLoadRecent"] = SETTINGS.refreshOnLoadRecent;
  doc["refreshOnLoadLibrary"] = SETTINGS.refreshOnLoadLibrary;
  doc["refreshOnLoadSettings"] = SETTINGS.refreshOnLoadSettings;
  doc["refreshOnLoadSync"] = SETTINGS.refreshOnLoadSync;
  doc["refreshOnLoadStatistics"] = SETTINGS.refreshOnLoadStatistics;
  doc["pageAutoTurnSeconds"] = READER_SETTINGS.pageAutoTurnSeconds;
  doc["bitmapRoundedCorners"] = SETTINGS.bitmapRoundedCorners;
  doc["opdsServerUrl"] = SETTINGS.opdsServerUrl;
  doc["opdsUsername"] = SETTINGS.opdsUsername;
  doc["opdsPasswordSet"] = strlen(SETTINGS.opdsPassword) > 0;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleLanguageGet() const {
  JsonDocument doc;
  doc["code"] = LanguageManager::activeLanguageCode();
  doc["name"] = LanguageManager::activeLanguageName();
  JsonArray installed = doc["installed"].to<JsonArray>();
  for (const LanguageManager::LanguageInfo& language : LanguageManager::installedLanguages()) {
    JsonObject item = installed.add<JsonObject>();
    item["code"] = language.code;
    item["name"] = language.name;
  }
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleLanguageUpdate() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  const char* code = doc["code"] | "";
  if (!LanguageManager::setLanguage(code)) {
    server->send(400, "text/plain", "Language is not installed");
    return;
  }
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void LocalServer::handleSettingsUpdate() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  bool changed = false;
  bool readerChanged = false;
  const bool clockAvailable = clockSettingsAvailable();

  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* key = kv.key().c_str();
    int value = kv.value().as<int>();

    if (strcmp(key, "sleepScreen") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::SLEEP_SCREEN_MODE_COUNT || (!clockAvailable && v == SystemSetting::DATETIME)) {
        v = SystemSetting::LIGHT;
      }
      SETTINGS.sleepScreen = v;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverMode") == 0) {
      SETTINGS.sleepScreenCoverMode = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverFilter") == 0) {
      SETTINGS.sleepScreenCoverFilter = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverGrayscale") == 0) {
      SETTINGS.sleepImageQuality = (value >= 0 && value < SystemSetting::SLEEP_IMAGE_QUALITY_COUNT)
                                       ? static_cast<uint8_t>(value)
                                       : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepImageQuality") == 0) {
      SETTINGS.sleepImageQuality = (value >= 0 && value < SystemSetting::SLEEP_IMAGE_QUALITY_COUNT)
                                       ? static_cast<uint8_t>(value)
                                       : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepImageTwoBit") == 0) {
      SETTINGS.sleepImageQuality = (uint8_t)value ? SystemSetting::SLEEP_IMAGE_MEDIUM : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepCustomBmp") == 0) {
      if (kv.value().isNull()) {
        SETTINGS.setSleepCustomBmpFromInput(nullptr);
      } else {
        SETTINGS.setSleepCustomBmpFromInput(kv.value().as<const char*>());
      }
      changed = true;
    } else if (clockAvailable && strcmp(key, "sleepClockStyle") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::SLEEP_CLOCK_STYLE_COUNT) v = SystemSetting::CLOCK_CENTERED_DATE;
      SETTINGS.sleepClockStyle = v;
      changed = true;
    } else if (clockAvailable && strcmp(key, "sleepClockTimeFormat") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::CLOCK_TIME_FORMAT_COUNT) v = SystemSetting::CLOCK_24_HOUR;
      SETTINGS.sleepClockTimeFormat = v;
      changed = true;
    } else if (clockAvailable && strcmp(key, "timeZoneQuarterOffset") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 104) v = 104;
      SETTINGS.timeZoneQuarterOffset = static_cast<uint8_t>(v);
      SETTINGS.timeZoneAutoDetectEnabled = 0;
      SETTINGS.timeZoneId[0] = '\0';
      changed = true;
    } else if (clockAvailable && strcmp(key, "timeZoneAutoDetectEnabled") == 0) {
      SETTINGS.timeZoneAutoDetectEnabled = value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "hideBatteryPercentage") == 0) {
      SETTINGS.hideBatteryPercentage = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "recentLibraryMode") == 0) {
      SETTINGS.recentLibraryMode = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "libraryMode") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::LIBRARY_MODE_COUNT) v = SystemSetting::LIBRARY_LIST;
      SETTINGS.libraryMode = v;
      changed = true;
    } else if (strcmp(key, "frontButtonLayout") == 0) {
      int v = static_cast<int>(value);
      if (v < 0 || v >= SystemSetting::FRONT_BUTTON_LAYOUT_COUNT) {
        v = SystemSetting::BACK_CONFIRM_LEFT_RIGHT;
      }
      SETTINGS.frontButtonLayout = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "recentVisibleCount") == 0) {
      int v = static_cast<int>(value);
      if (v < 1) v = 1;
      if (v > 9) v = 9;
      SETTINGS.recentVisibleCount = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "librarySortEnabled") == 0) {
      SETTINGS.librarySortEnabled = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "librarySortMode") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 6) v = 0;
      SETTINGS.librarySortMode = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "fontFamily") == 0) {
      READER_SETTINGS.fontFamily = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "fontSize") == 0) {
      READER_SETTINGS.fontSize = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "lineHeight") == 0) {
      uint8_t v = (uint8_t)value;
      READER_SETTINGS.lineHeight = (v < 10 || v > 200) ? 100 : v;
      readerChanged = true;
    } else if (strcmp(key, "textSpace") == 0) {
      uint8_t v = (uint8_t)value;
      READER_SETTINGS.textSpace = (v < 10 || v > 200) ? 100 : v;
      readerChanged = true;
    } else if (strcmp(key, "screenMargin") == 0) {
      READER_SETTINGS.screenMargin = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "paragraphAlignment") == 0) {
      READER_SETTINGS.paragraphAlignment = (uint8_t)value;
      if (READER_SETTINGS.paragraphAlignment >= SystemSetting::PARAGRAPH_ALIGNMENT_COUNT) {
        READER_SETTINGS.paragraphAlignment = SystemSetting::JUSTIFIED;
      }
      readerChanged = true;
    } else if (strcmp(key, "extraParagraphSpacing") == 0) {
      READER_SETTINGS.extraParagraphSpacing = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "paragraphCssIndentEnabled") == 0) {
      READER_SETTINGS.paragraphCssIndentEnabled = (uint8_t)value ? 1 : 0;
      readerChanged = true;
    } else if (strcmp(key, "orientation") == 0) {
      READER_SETTINGS.orientation = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "hyphenationEnabled") == 0) {
      READER_SETTINGS.hyphenationEnabled = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "bionicReadingEnabled") == 0) {
      READER_SETTINGS.bionicReadingEnabled = (uint8_t)value ? 1 : 0;
      readerChanged = true;
    } else if (strcmp(key, "shakePageTurn") == 0) {
      const int motionMode = static_cast<int>(value);
      SETTINGS.shakePageTurn = static_cast<uint8_t>(motionMode < 0 ? 0 : motionMode > 2 ? 2 : motionMode);
      changed = true;
    } else if (strcmp(key, "shakePageTurnSensitivity") == 0) {
      const int sensitivity = static_cast<int>(value);
      SETTINGS.shakePageTurnSensitivity = static_cast<uint8_t>(sensitivity < 0 ? 0 : sensitivity > 2 ? 2 : sensitivity);
      changed = true;
    } else if (strcmp(key, "textAntiAliasing") == 0) {
      READER_SETTINGS.textAntiAliasing = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "refreshFrequency") == 0) {
      READER_SETTINGS.refreshFrequency = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "readerImageGrayscale") == 0) {
      READER_SETTINGS.readerImageGrayscale = (value >= 0 && value < SystemSetting::READER_IMAGE_QUALITY_COUNT)
                                          ? (uint8_t)value
                                          : SystemSetting::READER_IMAGE_LOW;
      readerChanged = true;
    } else if (strcmp(key, "readerSmartRefreshOnImages") == 0) {
      READER_SETTINGS.readerSmartRefreshOnImages = (uint8_t)value ? 1 : 0;
      readerChanged = true;
    } else if (strcmp(key, "statusBar") == 0) {
      READER_SETTINGS.statusBar = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "statusBarLeft") == 0) {
      READER_SETTINGS.statusBarLeft = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "statusBarMiddle") == 0) {
      READER_SETTINGS.statusBarMiddle = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "statusBarRight") == 0) {
      READER_SETTINGS.statusBarRight = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "statusBarFullStyle") == 0) {
      READER_SETTINGS.statusBarFullStyle = (uint8_t)value;
      readerChanged = true;
    } else if (strcmp(key, "shortPwrBtn") == 0) {
      SETTINGS.shortPwrBtn = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepTimeout") == 0) {
      SETTINGS.sleepTimeout = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "useLibraryIndex") == 0) {
      SETTINGS.useLibraryIndex = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "libraryShelfEnabled") == 0) {
      SETTINGS.libraryShelfEnabled = (uint8_t)value ? 1 : 0;
      if (!SETTINGS.libraryShelfEnabled && SETTINGS.libraryViewMode == SystemSetting::LIBRARY_VIEW_SHELF) {
        SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
      }
      changed = true;
    } else if (strcmp(key, "bootSetting") == 0) {
      SETTINGS.bootSetting = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadRecent") == 0) {
      SETTINGS.refreshOnLoadRecent = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadLibrary") == 0) {
      SETTINGS.refreshOnLoadLibrary = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadSettings") == 0) {
      SETTINGS.refreshOnLoadSettings = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadSync") == 0) {
      SETTINGS.refreshOnLoadSync = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadStatistics") == 0) {
      SETTINGS.refreshOnLoadStatistics = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "pageAutoTurnSeconds") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 180) v = 180;
      v = (v / 10) * 10;
      READER_SETTINGS.pageAutoTurnSeconds = static_cast<uint8_t>(v);
      readerChanged = true;
    } else if (strcmp(key, "bitmapRoundedCorners") == 0) {
      int cornerStyle = static_cast<int>(value);
      if (cornerStyle < 0) cornerStyle = 0;
      if (cornerStyle > 2) cornerStyle = 2;
      SETTINGS.bitmapRoundedCorners = static_cast<uint8_t>(cornerStyle);
      changed = true;
    } else if (strcmp(key, "opdsServerUrl") == 0) {
      copySettingString(SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl), kv.value().as<const char*>());
      changed = true;
    } else if (strcmp(key, "opdsUsername") == 0) {
      copySettingString(SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), kv.value().as<const char*>());
      changed = true;
    } else if (strcmp(key, "opdsPassword") == 0) {
      copySettingString(SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), kv.value().as<const char*>());
      changed = true;
    }
  }

  if (changed) {
    SETTINGS.saveToFile();
    INX_SERIAL.printf("[%lu] [WEB] Settings updated and saved\n", millis());
  }
  if (readerChanged) {
    READER_SETTINGS.saveToFile();
    INX_SERIAL.printf("[%lu] [WEB] Reader settings updated and saved\n", millis());
  }

  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void LocalServer::handleWifiGet() const {
  JsonDocument doc;
  const auto& creds = WIFI_STORE.getCredentials();
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& cred : creds) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
  }
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleWifiPost() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));
  String ssid = doc["ssid"];
  String password = doc["password"] | "";

  if (WIFI_STORE.addCredential(ssid.c_str(), password.c_str())) {
    WIFI_STORE.saveToFile();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(500, "text/plain", "Failed to save");
  }
}

void LocalServer::handleWifiDelete() const {
  String uri = server->uri();
  int lastSlash = uri.lastIndexOf('/');
  String ssid = uri.substring(lastSlash + 1);
  ssid.replace("%20", " ");

  if (WIFI_STORE.removeCredential(ssid.c_str())) {
    WIFI_STORE.saveToFile();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(404, "text/plain", "Not found");
  }
}

void LocalServer::handleKOReaderGet() const {
  JsonDocument doc;
  doc["username"] = KOREADER_STORE.getUsername();
  doc["serverUrl"] = KOREADER_STORE.getServerUrl();
  doc["matchMethod"] = (int)KOREADER_STORE.getMatchMethod();
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleKOReaderPost() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));

  String username = doc["username"] | "";
  String password = doc["password"].is<const char*>() ? (doc["password"] | "") : KOREADER_STORE.getPassword().c_str();
  String serverUrl = doc["serverUrl"] | "";
  int matchMethod = doc["matchMethod"] | 0;

  KOREADER_STORE.setCredentials(username.c_str(), password.c_str());
  KOREADER_STORE.setServerUrl(serverUrl.c_str());
  KOREADER_STORE.setMatchMethod((DocumentMatchMethod)matchMethod);
  KOREADER_STORE.saveToFile();

  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

#ifndef INX_SIMULATOR_WEB_ONLY
void LocalServer::handleGeminiGet() const {
  JsonDocument doc;
  doc["configured"] = GeminiTranscription::configured();
  doc["apiKeyLast4"] = GeminiTranscription::apiKeyLast4();
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleGeminiPost() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  const String apiKey = doc["apiKey"] | "";
  if (!GeminiTranscription::saveApiKey(apiKey.c_str())) {
    server->send(500, "text/plain", "Failed to save Gemini API key");
    return;
  }
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void LocalServer::handleGeminiDelete() const {
  if (!GeminiTranscription::clearApiKey()) {
    server->send(500, "text/plain", "Failed to clear Gemini API key");
    return;
  }
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}
#endif

void LocalServer::handleFontsRescan() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(501, "application/json", "{\"ok\":false,\"error\":\"unavailable_in_simulator\"}");
#else
  if (!SdMan.ready()) {
    server->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }
  const bool ok = FontManager::scanSDFonts("/fonts", true);
  if (ok) {
    server->send(200, "application/json", "{\"ok\":true}");
  } else {
    server->send(500, "application/json", "{\"ok\":false,\"error\":\"scan_failed\"}");
  }
#endif
}

#ifndef INX_SIMULATOR_WEB_ONLY
void LocalServer::handleOpdsGet() const {
  JsonDocument doc;
  const auto& servers = OPDS_STORE.getAllServers();
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& srv : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = srv.name;
    obj["url"] = srv.url;
    obj["username"] = srv.username;
  }
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleOpdsPost() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));
  String name = doc["name"];
  String url = doc["url"];
  String username = doc["username"] | "";
  String password = doc["password"] | "";

  if (name.length() == 0 || url.length() == 0) {
    server->send(400, "text/plain", "Name and URL are required");
    return;
  }

  if (OPDS_STORE.addServer(name.c_str(), url.c_str(), username.c_str(), password.c_str())) {
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(500, "text/plain", "Failed to save");
  }
}

void LocalServer::handleOpdsDelete() const {
  String uri = server->uri();
  int lastSlash = uri.lastIndexOf('/');
  String name = uri.substring(lastSlash + 1);
  name.replace("%20", " ");

  if (OPDS_STORE.removeServer(name.c_str())) {
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(404, "text/plain", "Not found");
  }
}
#endif
