#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

class DictionaryWordSelectActivity final : public Activity {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int fontId, int marginLeft, int marginTop,
                                        std::string cachePath, uint8_t orientation,
                                        std::string nextPageFirstWord = "")
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        fontId(fontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        cachePath(std::move(cachePath)),
        orientation(orientation),
        nextPageFirstWord(std::move(nextPageFirstWord)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct WordInfo {
    std::string text;
    std::string lookupText;
    int16_t screenX;
    int16_t screenY;
    int16_t width;
    int16_t row;
    int continuationIndex;
    int continuationOf;

    WordInfo(const std::string& t, int16_t x, int16_t y, int16_t w, int16_t r)
        : text(t), lookupText(t), screenX(x), screenY(y), width(w), row(r), continuationIndex(-1), continuationOf(-1) {}
  };

  struct Row {
    int16_t yPos;
    std::vector<int> wordIndices;
  };

  std::unique_ptr<Page> page;
  int fontId;
  int marginLeft;
  int marginTop;
  std::string cachePath;
  uint8_t orientation;
  std::string nextPageFirstWord;

  std::vector<WordInfo> words;
  std::vector<Row> rows;
  int currentRow = 0;
  int currentWordInRow = 0;

  bool isLandscape() const;
  bool isInverted() const;
  void extractWords();
  void mergeHyphenatedWords();
};
