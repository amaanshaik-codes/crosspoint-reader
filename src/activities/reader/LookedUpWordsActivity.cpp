#include "LookedUpWordsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "DictionaryDefinitionActivity.h"
#include "DictionarySuggestionsActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"
#include "util/LookupHistory.h"

void LookedUpWordsActivity::onEnter() {
  Activity::onEnter();
  words = LookupHistory::load(cachePath);
  std::reverse(words.begin(), words.end());
  requestUpdate();
}

void LookedUpWordsActivity::loop() {
  if (words.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (deleteConfirmMode) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (ignoreNextConfirmRelease) {
        ignoreNextConfirmRelease = false;
      } else {
        LookupHistory::removeWord(cachePath, words[pendingDeleteIndex]);
        words.erase(words.begin() + pendingDeleteIndex);
        if (selectedIndex >= static_cast<int>(words.size())) {
          selectedIndex = std::max(0, static_cast<int>(words.size()) - 1);
        }
        deleteConfirmMode = false;
        requestUpdate();
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      deleteConfirmMode = false;
      ignoreNextConfirmRelease = false;
      requestUpdate();
    }
    return;
  }

  constexpr unsigned long DELETE_HOLD_MS = 700;
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= DELETE_HOLD_MS) {
    deleteConfirmMode = true;
    ignoreNextConfirmRelease = true;
    pendingDeleteIndex = selectedIndex;
    requestUpdate();
    return;
  }

  const int totalItems = static_cast<int>(words.size());
  const int pageItems = getPageItems();

  buttonNavigator.onNextRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const std::string& headword = words[selectedIndex];

    Rect popupLayout;
    {
      RenderLock lock(*this);
      popupLayout = GUI.drawPopup(renderer, "Looking up...");
    }

    std::string definition = Dictionary::lookup(headword, [this, &popupLayout](int percent) {
      RenderLock lock(*this);
      GUI.fillPopupProgress(renderer, popupLayout, percent);
    });

    if (!definition.empty()) {
      startActivityForResult(
          std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, headword, std::move(definition),
                                                         readerFontId),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              setResult(ActivityResult{});
              finish();
              return;
            }
            requestUpdate();
          });
      return;
    }

    auto stems = Dictionary::getStemVariants(headword);
    for (const auto& stem : stems) {
      std::string stemDef = Dictionary::lookup(stem);
      if (!stemDef.empty()) {
        startActivityForResult(
            std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, stem, std::move(stemDef),
                                                           readerFontId),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                setResult(ActivityResult{});
                finish();
                return;
              }
              requestUpdate();
            });
        return;
      }
    }

    auto similar = Dictionary::findSimilar(headword, 6);
    if (!similar.empty()) {
      startActivityForResult(
          std::make_unique<DictionarySuggestionsActivity>(renderer, mappedInput, headword, std::move(similar),
                                                          readerFontId),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              setResult(ActivityResult{});
              finish();
              return;
            }
            requestUpdate();
          });
      return;
    }

    {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, "Not found");
    }
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
}

int LookedUpWordsActivity::getPageItems() const {
  const auto orient = renderer.getOrientation();
  const auto metrics = UITheme::getInstance().getMetrics();
  const bool isInverted = orient == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isInverted ? (metrics.buttonHintsHeight + metrics.verticalSpacing) : 0;
  const int contentTop = hintGutterHeight + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return std::max(1, contentHeight / metrics.listRowHeight);
}

void LookedUpWordsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto orient = renderer.getOrientation();
  const auto metrics = UITheme::getInstance().getMetrics();
  const bool isLandscapeCw = orient == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orient == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orient == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int hintGutterHeight = isInverted ? (metrics.buttonHintsHeight + metrics.verticalSpacing) : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{contentX, hintGutterHeight + metrics.topPadding, pageWidth - hintGutterWidth, metrics.headerHeight},
                 "Lookup History");

  const int contentTop = hintGutterHeight + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (words.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 20, "No words looked up yet");
  } else {
    GUI.drawList(renderer, Rect{contentX, contentTop, pageWidth - hintGutterWidth, contentHeight}, words.size(),
                 selectedIndex, [this](int index) { return words[index]; }, nullptr, nullptr, nullptr);
  }

  if (deleteConfirmMode && pendingDeleteIndex < static_cast<int>(words.size())) {
    const std::string& word = words[pendingDeleteIndex];
    std::string displayWord = word;
    if (displayWord.size() > 20) {
      displayWord.erase(17);
      displayWord += "...";
    }
    const std::string msg = "Delete '" + displayWord + "'?";

    constexpr int margin = 15;
    const int popupY = 200 + hintGutterHeight;
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, msg.c_str(), EpdFontFamily::BOLD);
    const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int w = textWidth + margin * 2;
    const int h = textHeight + margin * 2;
    const int x = contentX + (renderer.getScreenWidth() - hintGutterWidth - w) / 2;

    renderer.fillRect(x - 2, popupY - 2, w + 4, h + 4, true);
    renderer.fillRect(x, popupY, w, h, false);

    const int textX = x + (w - textWidth) / 2;
    const int textY = popupY + margin - 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, msg.c_str(), true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_DELETE_CACHE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    if (!words.empty()) {
      const char* deleteHint = "Hold select to delete";
      const int hintWidth = renderer.getTextWidth(SMALL_FONT_ID, deleteHint);
      const int hintX = contentX + (renderer.getScreenWidth() - hintGutterWidth - hintWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, hintX,
                        renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing * 2,
                        deleteHint);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
