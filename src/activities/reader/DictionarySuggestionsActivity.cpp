#include "DictionarySuggestionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"

void DictionarySuggestionsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void DictionarySuggestionsActivity::loop() {
  if (suggestions.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(suggestions.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(suggestions.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const std::string& selected = suggestions[selectedIndex];

    Rect popupLayout;
    {
      RenderLock lock(*this);
      popupLayout = GUI.drawPopup(renderer, "Looking up...");
    }

    std::string definition = Dictionary::lookup(selected, [this, &popupLayout](int percent) {
      RenderLock lock(*this);
      GUI.fillPopupProgress(renderer, popupLayout, percent);
    });

    if (definition.empty()) {
      {
        RenderLock lock(*this);
        GUI.drawPopup(renderer, "Not found");
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      requestUpdate();
      return;
    }

    startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, selected,
                                                                          std::move(definition), readerFontId),
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
}

void DictionarySuggestionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto orient = renderer.getOrientation();
  const auto metrics = UITheme::getInstance().getMetrics();
  const bool isLandscapeCw = orient == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orient == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orient == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int hintGutterHeight = isInverted ? (metrics.buttonHintsHeight + metrics.verticalSpacing) : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int leftPadding = contentX + metrics.contentSidePadding;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{contentX, hintGutterHeight + metrics.topPadding, pageWidth - hintGutterWidth, metrics.headerHeight},
                 "Did you mean?");

  const int subtitleY = hintGutterHeight + metrics.topPadding + metrics.headerHeight + 5;
  const std::string subtitle = "\"" + originalWord + "\" not found";
  renderer.drawText(SMALL_FONT_ID, leftPadding, subtitleY, subtitle.c_str());

  const int listTop = subtitleY + 25;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (suggestions.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + 20, "No suggestions");
  } else {
    GUI.drawList(renderer, Rect{contentX, listTop, pageWidth - hintGutterWidth, listHeight}, suggestions.size(),
                 selectedIndex, [this](int index) { return suggestions[index]; }, nullptr, nullptr, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
