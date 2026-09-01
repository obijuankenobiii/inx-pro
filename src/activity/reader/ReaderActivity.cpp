/**
 * @file ReaderActivity.cpp
 * @brief Definitions for ReaderActivity.
 */

#include "ReaderActivity.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>

#include "Epub.h"
#include "Epub/EpubActivity.h"
#include "Mobi.h"
#include "Pdf.h"
#include "PdfReaderActivity.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "system/FontManager.h"
#include "system/ScreenComponents.h"
#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

/**
 * @brief Extracts the parent directory path from a file path
 * @param filePath Full path to a file
 * @return Path to the containing directory
 */
std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

/**
 * @brief Checks if the file is an XTC format file
 * @param path Path to the file
 * @return true if file has .xtc or .xtch extension
 */
bool ReaderActivity::isXtcFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch");
}

/**
 * @brief Checks if the file is a plain text file
 * @param path Path to the file
 * @return true if file has .txt or .md extension
 */
bool ReaderActivity::isTxtFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".txt") || StringUtils::checkFileExtension(path, ".md");
}

/**
 * @brief Checks if the file is a classic MOBI6 format file
 * @param path Path to the file
 * @return true if file has a .mobi extension
 */
bool ReaderActivity::isMobiFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".mobi"); }

/**
 * @brief Checks if the file is a PDF file
 * @param path Path to the file
 * @return true if file has a .pdf extension
 */
bool ReaderActivity::isPdfFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".pdf"); }

/**
 * @brief Loads an EPUB file from the given path
 * @param path Path to the EPUB file
 * @return Unique pointer to loaded Epub object, or nullptr if loading fails
 */
std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  INX_SERIAL.printf("[%lu] [THUMB-TRACE] ReaderActivity::loadEpub path=%s\n", millis(), path.c_str());
  SdIoMutex::Lock lock;
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.metadata"));
  const bool hadMetadataCache = epub->hasMetadataCache();
  if (epub->load(false)) {
    return epub;
  }

  if (hadMetadataCache) {
    INX_SERIAL.printf("[Reader] EPUB metadata cache failed, rebuilding: %s\n", path.c_str());
    epub->clearCache();
  }

  return epub;
}

/**
 * @brief Loads an XTC file from the given path
 * @param path Path to the XTC file
 * @return Unique pointer to loaded Xtc object, or nullptr if loading fails
 */
std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  SdIoMutex::Lock lock;
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.metadata/xtc"));
  if (xtc->load()) {
    return xtc;
  }

  return nullptr;
}

/**
 * @brief Loads a TXT file from the given path
 * @param path Path to the TXT file
 * @return Unique pointer to loaded Txt object, or nullptr if loading fails
 */
std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  SdIoMutex::Lock lock;
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.system"));
  if (txt->load()) {
    return txt;
  }

  return nullptr;
}

/**
 * @brief Loads a PDF file from the given path
 * @param path Path to the PDF file
 * @return Unique pointer to loaded Pdf object, or nullptr if loading fails
 */
std::unique_ptr<Pdf> ReaderActivity::loadPdf(const std::string& path) {
  SdIoMutex::Lock lock;
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto pdf = std::unique_ptr<Pdf>(new Pdf(path, "/.metadata/pdf"));
  if (pdf->load()) {
    return pdf;
  }

  return nullptr;
}

/**
 * @brief Loads a MOBI file by transcoding it (once, then cached) into a minimal EPUB and loading that
 * @param path Path to the MOBI file
 * @return Unique pointer to loaded Epub object (backed by the transcoded cache file), or nullptr on failure
 */
std::unique_ptr<Epub> ReaderActivity::loadEpubFromMobi(const std::string& path) {
  SdIoMutex::Lock lock;
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  const std::string cacheDir = "/.metadata/mobi";
  SdMan.mkdir(cacheDir.c_str());
  const std::string cachedEpubPath = cacheDir + "/" + std::to_string(std::hash<std::string>{}(path)) + ".epub";

  if (!SdMan.exists(cachedEpubPath.c_str())) {
    if (!Mobi::convertToEpub(path, cachedEpubPath)) {
      return nullptr;
    }
  }

  return loadEpub(cachedEpubPath);
}

/**
 * @brief Transitions to the EPUB reader activity with the loaded EPUB
 * @param epub Unique pointer to loaded Epub object
 */
void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  std::string bookPath = epub->getPath();

  auto callback = onGoBack;

  exitActivity();
  FontManager::unloadAllSDFonts();
  enterNewActivity(new EpubActivity(
      renderer, mappedInput, std::move(epub),
      [callback, bookPath] {
        if (callback) {
          callback(bookPath);
        }
      },
      [] {}, initialSpineIndex, initialPageNumber));
}

/**
 * @brief Transitions to the XTC reader activity with the loaded XTC
 * @param xtc Unique pointer to loaded Xtc object
 */
void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { onGoBack(xtcPath); }, [this] { onGoBack(""); }));
}

/**
 * @brief Transitions to the TXT reader activity with the loaded TXT
 * @param txt Unique pointer to loaded Txt object
 */
void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, mappedInput, std::move(txt),
      [this] {
        if (onGoBack) {
          onGoBack(currentBookPath);
        }
      },
      [] {}));
}

/**
 * @brief Transitions to the PDF reader activity with the loaded PDF
 * @param pdf Unique pointer to loaded Pdf object
 */
void ReaderActivity::onGoToPdfReader(std::unique_ptr<Pdf> pdf) {
  const auto pdfPath = pdf->getPath();
  currentBookPath = pdfPath;
  exitActivity();
  enterNewActivity(new PdfReaderActivity(
      renderer, mappedInput, std::move(pdf), [this, pdfPath] { onGoBack(pdfPath); }, [this] { onGoBack(""); }));
}

void ReaderActivity::showCorruptedBookError() {
  renderer.clearScreen();
  ScreenComponents::drawPopup(renderer, "Failed to open book.");
  vTaskDelay(pdMS_TO_TICKS(1200));

  if (onGoBack) {
    onGoBack(currentBookPath);
  }
}

/**
 * @brief Called when entering the reader activity
 */
void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  INX_SERIAL.printf("[%lu] [THUMB-TRACE] ReaderActivity::onEnter path=%s\n", millis(), initialBookPath.c_str());

  if (initialBookPath.empty()) {
    if (onGoBack) {
      onGoBack("");
    }
    return;
  }

  currentBookPath = initialBookPath;

  if (isXtcFile(initialBookPath)) {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] reader branch=XTC path=%s\n", millis(), initialBookPath.c_str());
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      showCorruptedBookError();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] reader branch=TXT path=%s\n", millis(), initialBookPath.c_str());
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      showCorruptedBookError();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else if (isPdfFile(initialBookPath)) {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] reader branch=PDF path=%s\n", millis(), initialBookPath.c_str());
    auto pdf = loadPdf(initialBookPath);
    if (!pdf) {
      showCorruptedBookError();
      return;
    }
    onGoToPdfReader(std::move(pdf));
  } else if (isMobiFile(initialBookPath)) {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] reader branch=MOBI path=%s\n", millis(), initialBookPath.c_str());
    auto epub = loadEpubFromMobi(initialBookPath);
    if (!epub) {
      showCorruptedBookError();
      return;
    }
    onGoToEpubReader(std::move(epub));
  } else {
    INX_SERIAL.printf("[%lu] [THUMB-TRACE] reader branch=EPUB path=%s\n", millis(), initialBookPath.c_str());
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      showCorruptedBookError();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}
