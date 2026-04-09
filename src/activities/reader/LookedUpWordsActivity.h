#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LookedUpWordsActivity final : public Activity {
 public:
  explicit LookedUpWordsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string cachePath,
                                 int readerFontId)
      : Activity("LookedUpWords", renderer, mappedInput),
        cachePath(std::move(cachePath)),
        readerFontId(readerFontId) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string cachePath;
  int readerFontId;

  std::vector<std::string> words;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  bool deleteConfirmMode = false;
  bool ignoreNextConfirmRelease = false;
  int pendingDeleteIndex = 0;

  int getPageItems() const;
};
