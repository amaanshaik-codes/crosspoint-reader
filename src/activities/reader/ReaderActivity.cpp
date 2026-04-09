#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>
#include <vector>

#include "CrossPointSettings.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"

namespace {

std::string resolveBookPath(const std::string& path) {
  if (path.empty()) {
    return path;
  }

  if (Storage.exists(path.c_str())) {
    return path;
  }

  if (!path.empty() && path.front() == '/') {
    const std::string withoutLeading = path.substr(1);
    if (!withoutLeading.empty() && Storage.exists(withoutLeading.c_str())) {
      return withoutLeading;
    }
    return path;
  }

  const std::string withLeading = "/" + path;
  if (Storage.exists(withLeading.c_str())) {
    return withLeading;
  }

  return path;
}

std::vector<std::string> listOtherEpubCaches(const std::string& keepCachePath) {
  std::vector<std::string> cachePaths;

  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return cachePaths;
  }

  char name[128];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (file.isDirectory() && strncmp(name, "epub_", 5) == 0) {
      std::string fullPath = std::string("/.crosspoint/") + name;
      if (fullPath != keepCachePath) {
        cachePaths.emplace_back(std::move(fullPath));
      }
    }
    file.close();
  }
  root.close();

  std::sort(cachePaths.begin(), cachePaths.end());
  return cachePaths;
}

bool retryEpubLoadAfterCacheEviction(Epub* epub) {
  if (!epub) {
    return false;
  }

  const auto cachePaths = listOtherEpubCaches(epub->getCachePath());
  for (const auto& cachePath : cachePaths) {
    LOG_ERR("READER", "EPUB load retry: evicting cache %s", cachePath.c_str());
    if (!Storage.removeDir(cachePath.c_str())) {
      LOG_ERR("READER", "Failed to evict cache %s", cachePath.c_str());
      continue;
    }

    epub->clearCache();
    if (epub->load(true, true)) {
      LOG_DBG("READER", "Recovered EPUB load after evicting cache: %s", cachePath.c_str());
      return true;
    }
  }

  return false;
}

}  // namespace

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isBmpFile(const std::string& path) { return FsHelpers::hasBmpExtension(path); }

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  const std::string resolvedPath = resolveBookPath(path);

  if (!Storage.exists(resolvedPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", resolvedPath.c_str());
    return nullptr;
  }

  Storage.mkdir("/.crosspoint");

  auto epub = std::unique_ptr<Epub>(new Epub(resolvedPath, "/.crosspoint"));
  if (epub->load(true, SETTINGS.embeddedStyle == 0)) {
    return epub;
  }

  // Retry in a safer mode: clear potentially stale cache and skip CSS parsing.
  LOG_ERR("READER", "Initial EPUB load failed, retrying with rebuilt cache and CSS disabled: %s", resolvedPath.c_str());
  epub->clearCache();
  if (epub->load(true, true)) {
    return epub;
  }

  if (retryEpubLoadAfterCacheEviction(epub.get())) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load EPUB after retry: %s", resolvedPath.c_str());
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  const std::string resolvedPath = resolveBookPath(path);

  if (!Storage.exists(resolvedPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", resolvedPath.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(resolvedPath, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  const std::string resolvedPath = resolveBookPath(path);

  if (!Storage.exists(resolvedPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", resolvedPath.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(resolvedPath, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(resolveBookPath(fromBookPath));
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub)));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
