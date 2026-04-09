#include "DictionaryDefinitionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::wrapText() {
  wrappedLines.clear();

  const auto orient = renderer.getOrientation();
  const auto metrics = UITheme::getInstance().getMetrics();
  const bool isLandscapeCw = orient == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orient == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orient == GfxRenderer::Orientation::PortraitInverted;
  hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  hintGutterHeight = isInverted ? (metrics.buttonHintsHeight + metrics.verticalSpacing) : 0;
  contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int sidePadding = metrics.contentSidePadding;
  leftPadding = contentX + sidePadding;
  rightPadding = (isLandscapeCcw ? hintGutterWidth : 0) + sidePadding;

  const int screenWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(readerFontId);
  const int maxWidth = screenWidth - leftPadding - rightPadding;
  const int topArea = hintGutterHeight + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottomArea = metrics.buttonHintsHeight + metrics.verticalSpacing;

  linesPerPage = (renderer.getScreenHeight() - topArea - bottomArea) / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  std::string currentLine;
  std::string currentWord;

  for (size_t i = 0; i <= definition.size(); i++) {
    const char c = (i < definition.size()) ? definition[i] : '\0';

    if (c == '\n' || c == '\0') {
      if (!currentWord.empty()) {
        if (currentLine.empty()) {
          currentLine = currentWord;
        } else {
          const std::string test = currentLine + " " + currentWord;
          if (renderer.getTextWidth(readerFontId, test.c_str()) <= maxWidth) {
            currentLine = test;
          } else {
            wrappedLines.push_back(currentLine);
            currentLine = currentWord;
          }
        }
        currentWord.clear();
      }
      wrappedLines.push_back(currentLine);
      currentLine.clear();
    } else if (c == ' ') {
      if (!currentWord.empty()) {
        if (currentLine.empty()) {
          currentLine = currentWord;
        } else {
          const std::string test = currentLine + " " + currentWord;
          if (renderer.getTextWidth(readerFontId, test.c_str()) <= maxWidth) {
            currentLine = test;
          } else {
            wrappedLines.push_back(currentLine);
            currentLine = currentWord;
          }
        }
        currentWord.clear();
      }
    } else {
      currentWord += c;
    }
  }

  totalPages = (static_cast<int>(wrappedLines.size()) + linesPerPage - 1) / linesPerPage;
  if (totalPages < 1) totalPages = 1;
}

void DictionaryDefinitionActivity::loop() {
  const bool prevPage = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextPage = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Right);

  if (prevPage && currentPage > 0) {
    currentPage--;
    requestUpdate();
  }

  if (nextPage && currentPage < totalPages - 1) {
    currentPage++;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(ActivityResult{});
    finish();
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

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(readerFontId);

  GUI.drawHeader(renderer,
                 Rect{contentX, hintGutterHeight + metrics.topPadding, renderer.getScreenWidth() - hintGutterWidth,
                      metrics.headerHeight},
                 headword.c_str());
  const int bodyStartY = hintGutterHeight + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const int startLine = currentPage * linesPerPage;
  for (int i = 0; i < linesPerPage && (startLine + i) < static_cast<int>(wrappedLines.size()); i++) {
    const int y = bodyStartY + i * lineHeight;
    renderer.drawText(readerFontId, leftPadding, y, wrappedLines[startLine + i].c_str());
  }

  if (totalPages > 1) {
    const std::string pageInfo = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, pageInfo.c_str());
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - rightPadding - textWidth,
                      renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing,
                      pageInfo.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
