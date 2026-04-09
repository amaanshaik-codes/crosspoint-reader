#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class DictionarySuggestionsActivity final : public Activity {
 public:
  explicit DictionarySuggestionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string originalWord, std::vector<std::string> suggestions,
                                         int readerFontId)
      : Activity("DictionarySuggestions", renderer, mappedInput),
        originalWord(std::move(originalWord)),
        suggestions(std::move(suggestions)),
        readerFontId(readerFontId) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string originalWord;
  std::vector<std::string> suggestions;
  int readerFontId;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};
