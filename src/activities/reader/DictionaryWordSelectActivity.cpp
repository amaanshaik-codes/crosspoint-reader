#include "DictionaryWordSelectActivity.h"

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <climits>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionarySuggestionsActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/Dictionary.h"
#include "util/LookupHistory.h"

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  extractWords();
  mergeHyphenatedWords();
  if (!rows.empty()) {
    currentRow = static_cast<int>(rows.size()) / 3;
    currentWordInRow = 0;
  }
  requestUpdate();
}

bool DictionaryWordSelectActivity::isLandscape() const {
  return orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CW ||
         orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW;
}

bool DictionaryWordSelectActivity::isInverted() const {
  return orientation == CrossPointSettings::ORIENTATION::INVERTED;
}

void DictionaryWordSelectActivity::extractWords() {
  words.clear();
  rows.clear();

  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) {
      continue;
    }

    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;

    const auto& wordList = block->getWords();
    const auto& xPosList = block->getWordXpos();
    const size_t count = std::min(wordList.size(), xPosList.size());

    for (size_t i = 0; i < count; i++) {
      const int16_t screenX = line.xPos + xPosList[i] + marginLeft;
      const int16_t screenY = line.yPos + marginTop;
      const std::string& wordText = wordList[i];

      std::vector<size_t> splitStarts;
      size_t partStart = 0;
      for (size_t byteIndex = 0; byteIndex < wordText.size();) {
        if (byteIndex + 2 < wordText.size() && static_cast<uint8_t>(wordText[byteIndex]) == 0xE2 &&
            static_cast<uint8_t>(wordText[byteIndex + 1]) == 0x80 &&
            (static_cast<uint8_t>(wordText[byteIndex + 2]) == 0x93 ||
             static_cast<uint8_t>(wordText[byteIndex + 2]) == 0x94)) {
          if (byteIndex > partStart) {
            splitStarts.push_back(partStart);
          }
          byteIndex += 3;
          partStart = byteIndex;
        } else {
          byteIndex++;
        }
      }
      if (partStart < wordText.size()) {
        splitStarts.push_back(partStart);
      }

      if (splitStarts.size() <= 1 && partStart == 0) {
        const int16_t wordWidth = renderer.getTextWidth(fontId, wordText.c_str());
        words.push_back({wordText, screenX, screenY, wordWidth, 0});
      } else {
        for (size_t splitIndex = 0; splitIndex < splitStarts.size(); splitIndex++) {
          const size_t start = splitStarts[splitIndex];
          const size_t end = (splitIndex + 1 < splitStarts.size()) ? splitStarts[splitIndex + 1] : wordText.size();

          size_t textEnd = end;
          while (textEnd > start && textEnd <= wordText.size()) {
            if (textEnd >= 3 && static_cast<uint8_t>(wordText[textEnd - 3]) == 0xE2 &&
                static_cast<uint8_t>(wordText[textEnd - 2]) == 0x80 &&
                (static_cast<uint8_t>(wordText[textEnd - 1]) == 0x93 ||
                 static_cast<uint8_t>(wordText[textEnd - 1]) == 0x94)) {
              textEnd -= 3;
            } else {
              break;
            }
          }

          std::string part = wordText.substr(start, textEnd - start);
          if (part.empty()) continue;

          std::string prefix = wordText.substr(0, start);
          const int16_t offsetX = prefix.empty() ? 0 : renderer.getTextWidth(fontId, prefix.c_str());
          const int16_t partWidth = renderer.getTextWidth(fontId, part.c_str());
          words.push_back({part, static_cast<int16_t>(screenX + offsetX), screenY, partWidth, 0});
        }
      }
    }
  }

  if (words.empty()) return;

  int16_t currentY = words[0].screenY;
  rows.push_back({currentY, {}});

  for (size_t i = 0; i < words.size(); i++) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back({currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

void DictionaryWordSelectActivity::mergeHyphenatedWords() {
  for (size_t rowIndex = 0; rowIndex + 1 < rows.size(); rowIndex++) {
    if (rows[rowIndex].wordIndices.empty() || rows[rowIndex + 1].wordIndices.empty()) {
      continue;
    }

    const int lastWordIdx = rows[rowIndex].wordIndices.back();
    const std::string& lastWord = words[lastWordIdx].text;
    if (lastWord.empty()) {
      continue;
    }

    bool endsWithHyphen = false;
    if (lastWord.back() == '-') {
      endsWithHyphen = true;
    } else if (lastWord.size() >= 2 && static_cast<uint8_t>(lastWord[lastWord.size() - 2]) == 0xC2 &&
               static_cast<uint8_t>(lastWord[lastWord.size() - 1]) == 0xAD) {
      endsWithHyphen = true;
    }

    if (!endsWithHyphen) {
      continue;
    }

    const int nextWordIdx = rows[rowIndex + 1].wordIndices.front();

    words[lastWordIdx].continuationIndex = nextWordIdx;
    words[nextWordIdx].continuationOf = lastWordIdx;

    std::string firstPart = lastWord;
    if (firstPart.back() == '-') {
      firstPart.pop_back();
    } else if (firstPart.size() >= 2 && static_cast<uint8_t>(firstPart[firstPart.size() - 2]) == 0xC2 &&
               static_cast<uint8_t>(firstPart[firstPart.size() - 1]) == 0xAD) {
      firstPart.erase(firstPart.size() - 2);
    }

    const std::string merged = firstPart + words[nextWordIdx].text;
    words[lastWordIdx].lookupText = merged;
    words[nextWordIdx].lookupText = merged;
    words[nextWordIdx].continuationIndex = nextWordIdx;
  }

  if (!nextPageFirstWord.empty() && !rows.empty()) {
    const int lastWordIdx = rows.back().wordIndices.back();
    const std::string& lastWord = words[lastWordIdx].text;
    if (!lastWord.empty()) {
      bool endsWithHyphen = false;
      if (lastWord.back() == '-') {
        endsWithHyphen = true;
      } else if (lastWord.size() >= 2 && static_cast<uint8_t>(lastWord[lastWord.size() - 2]) == 0xC2 &&
                 static_cast<uint8_t>(lastWord[lastWord.size() - 1]) == 0xAD) {
        endsWithHyphen = true;
      }

      if (endsWithHyphen) {
        std::string firstPart = lastWord;
        if (firstPart.back() == '-') {
          firstPart.pop_back();
        } else if (firstPart.size() >= 2 && static_cast<uint8_t>(firstPart[firstPart.size() - 2]) == 0xC2 &&
                   static_cast<uint8_t>(firstPart[firstPart.size() - 1]) == 0xAD) {
          firstPart.erase(firstPart.size() - 2);
        }
        words[lastWordIdx].lookupText = firstPart + nextPageFirstWord;
      }
    }
  }

  rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& row) { return row.wordIndices.empty(); }),
             rows.end());
}

void DictionaryWordSelectActivity::loop() {
  if (words.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  bool changed = false;
  const bool landscape = isLandscape();
  const bool inverted = isInverted();

  bool rowPrevPressed;
  bool rowNextPressed;
  bool wordPrevPressed;
  bool wordNextPressed;

  if (landscape && orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CW) {
    rowPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Left);
    rowNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Right);
    wordPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
    wordNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
  } else if (landscape) {
    rowPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Right);
    rowNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Left);
    wordPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
    wordNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
  } else if (inverted) {
    rowPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
    rowNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
    wordPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Right);
    wordNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Left);
  } else {
    rowPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
    rowNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
    wordPrevPressed = mappedInput.wasReleased(MappedInputManager::Button::Left);
    wordNextPressed = mappedInput.wasReleased(MappedInputManager::Button::Right);
  }

  const int rowCount = static_cast<int>(rows.size());

  auto findClosestWord = [&](int targetRow) {
    const int wordIdx = rows[currentRow].wordIndices[currentWordInRow];
    const int currentCenterX = words[wordIdx].screenX + words[wordIdx].width / 2;
    int bestMatch = 0;
    int bestDist = INT_MAX;
    for (int i = 0; i < static_cast<int>(rows[targetRow].wordIndices.size()); i++) {
      const int idx = rows[targetRow].wordIndices[i];
      const int centerX = words[idx].screenX + words[idx].width / 2;
      const int dist = std::abs(centerX - currentCenterX);
      if (dist < bestDist) {
        bestDist = dist;
        bestMatch = i;
      }
    }
    return bestMatch;
  };

  if (rowPrevPressed) {
    const int targetRow = (currentRow > 0) ? currentRow - 1 : rowCount - 1;
    currentWordInRow = findClosestWord(targetRow);
    currentRow = targetRow;
    changed = true;
  }

  if (rowNextPressed) {
    const int targetRow = (currentRow < rowCount - 1) ? currentRow + 1 : 0;
    currentWordInRow = findClosestWord(targetRow);
    currentRow = targetRow;
    changed = true;
  }

  if (wordPrevPressed) {
    if (currentWordInRow > 0) {
      currentWordInRow--;
    } else if (rowCount > 1) {
      currentRow = (currentRow > 0) ? currentRow - 1 : rowCount - 1;
      currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
    }
    changed = true;
  }

  if (wordNextPressed) {
    if (currentWordInRow < static_cast<int>(rows[currentRow].wordIndices.size()) - 1) {
      currentWordInRow++;
    } else if (rowCount > 1) {
      currentRow = (currentRow < rowCount - 1) ? currentRow + 1 : 0;
      currentWordInRow = 0;
    }
    changed = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int wordIdx = rows[currentRow].wordIndices[currentWordInRow];
    const std::string& rawWord = words[wordIdx].lookupText;
    const std::string cleaned = Dictionary::cleanWord(rawWord);

    if (cleaned.empty()) {
      {
        RenderLock lock(*this);
        GUI.drawPopup(renderer, "No word");
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      requestUpdate();
      return;
    }

    Rect popupLayout;
    {
      RenderLock lock(*this);
      popupLayout = GUI.drawPopup(renderer, "Looking up...");
    }

    bool cancelled = false;
    std::string definition = Dictionary::lookup(
        cleaned,
        [this, &popupLayout](int percent) {
          RenderLock lock(*this);
          GUI.fillPopupProgress(renderer, popupLayout, percent);
        },
        [this, &cancelled]() -> bool {
          mappedInput.update();
          if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            cancelled = true;
            return true;
          }
          return false;
        });

    if (cancelled) {
      requestUpdate();
      return;
    }

    LookupHistory::addWord(cachePath, cleaned);

    if (!definition.empty()) {
      startActivityForResult(
          std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, cleaned, std::move(definition), fontId),
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

    auto stems = Dictionary::getStemVariants(cleaned);
    for (const auto& stem : stems) {
      std::string stemDef = Dictionary::lookup(stem);
      if (!stemDef.empty()) {
        startActivityForResult(
            std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, stem, std::move(stemDef), fontId),
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

    auto similar = Dictionary::findSimilar(cleaned, 6);
    if (!similar.empty()) {
      startActivityForResult(std::make_unique<DictionarySuggestionsActivity>(renderer, mappedInput, cleaned,
                                                                             std::move(similar), fontId),
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

  if (changed) {
    requestUpdate();
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  page->render(renderer, fontId, marginLeft, marginTop);

  if (!words.empty() && currentRow < static_cast<int>(rows.size())) {
    const int wordIdx = rows[currentRow].wordIndices[currentWordInRow];
    const auto& w = words[wordIdx];

    const int lineHeight = renderer.getLineHeight(fontId);
    renderer.fillRect(w.screenX - 1, w.screenY - 1, w.width + 2, lineHeight + 2, true);
    renderer.drawText(fontId, w.screenX, w.screenY, w.text.c_str(), false);

    int otherIdx = (w.continuationOf >= 0) ? w.continuationOf : -1;
    if (otherIdx < 0 && w.continuationIndex >= 0 && w.continuationIndex != wordIdx) {
      otherIdx = w.continuationIndex;
    }
    if (otherIdx >= 0) {
      const auto& other = words[otherIdx];
      renderer.fillRect(other.screenX - 1, other.screenY - 1, other.width + 2, lineHeight + 2, true);
      renderer.drawText(fontId, other.screenX, other.screenY, other.text.c_str(), false);
    }
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
