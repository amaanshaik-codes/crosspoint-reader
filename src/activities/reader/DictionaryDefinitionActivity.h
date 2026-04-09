#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

class DictionaryDefinitionActivity final : public Activity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::string headword, std::string definition, int readerFontId)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        headword(std::move(headword)),
        definition(std::move(definition)),
        readerFontId(readerFontId) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string headword;
  std::string definition;
  int readerFontId;

  std::vector<std::string> wrappedLines;
  int currentPage = 0;
  int linesPerPage = 0;
  int totalPages = 0;

  int leftPadding = 20;
  int rightPadding = 20;
  int hintGutterHeight = 0;
  int contentX = 0;
  int hintGutterWidth = 0;

  void wrapText();
};
