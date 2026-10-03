#pragma once

#include <stddef.h>
#include <stdint.h>

namespace shelfclock {

class ScrollSequence {
 public:
  static const size_t kMaxTextLength = 512;
  static const size_t kVisibleGlyphs = 7;
  static const uint32_t kFrameIntervalMs = 250;

  bool begin(const char *text, uint32_t now);
  void cancel();
  bool active() const;
  bool hasFrame() const;
  bool frameDue(uint32_t now) const;
  uint16_t glyph(size_t visiblePosition) const;
  bool advance(uint32_t now);

 private:
  static const uint16_t kBlankGlyph = 96;
  static const size_t kPaddingGlyphs = 6;
  static const size_t kMaxTranslatedGlyphs = (kMaxTextLength * 2) + (kPaddingGlyphs * 2);

  static uint16_t translate(char character);

  uint16_t glyphs_[kMaxTranslatedGlyphs] = {};
  size_t frameCount_ = 0;
  size_t position_ = 0;
  uint32_t nextFrameAt_ = 0;
  bool active_ = false;
};

}  // namespace shelfclock
