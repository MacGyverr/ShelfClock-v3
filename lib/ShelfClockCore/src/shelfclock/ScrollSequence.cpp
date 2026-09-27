#include "ScrollSequence.h"

#include <string.h>

namespace shelfclock {

uint16_t ScrollSequence::translate(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'A' && character <= 'Z') {
    return 34 + (character - 'A');
  }
  if (character >= 'a' && character <= 'z') {
    return 66 + (character - 'a');
  }

  switch (character) {
    case '`':
    case '\'':
      return 17;
    case ',':
      return 22;
    case '-':
      return 23;
    case '.':
      return 24;
    case '%':
      return 15;
    case '^':
      return 26;
    case ':':
      return 27;
    case ' ':
    default:
      return 10;
  }
}

bool ScrollSequence::begin(const char *text, uint32_t now) {
  static const char fallback[] = "ArE U A HAckEr";
  const char *source = text == nullptr ? "" : text;
  size_t length = strlen(source);
  if (length > kMaxTextLength) {
    source = fallback;
    length = sizeof(fallback) - 1;
  }

  for (size_t index = 0; index < kPaddingGlyphs; index++) {
    glyphs_[index] = kBlankGlyph;
  }
  for (size_t index = 0; index < length; index++) {
    glyphs_[(index * 2) + kPaddingGlyphs] = translate(source[index]);
    glyphs_[(index * 2) + kPaddingGlyphs + 1] = kBlankGlyph;
  }
  const size_t trailingStart = (length * 2) + kPaddingGlyphs;
  for (size_t index = 0; index < kPaddingGlyphs; index++) {
    glyphs_[trailingStart + index] = kBlankGlyph;
  }

  frameCount_ = (length * 2) + kPaddingGlyphs;
  position_ = 0;
  nextFrameAt_ = now;
  active_ = true;
  return true;
}

void ScrollSequence::cancel() {
  active_ = false;
  frameCount_ = 0;
  position_ = 0;
}

bool ScrollSequence::active() const {
  return active_;
}

bool ScrollSequence::hasFrame() const {
  return active_ && position_ < frameCount_;
}

bool ScrollSequence::frameDue(uint32_t now) const {
  return active_ && static_cast<int32_t>(now - nextFrameAt_) >= 0;
}

uint16_t ScrollSequence::glyph(size_t visiblePosition) const {
  if (!active_ || visiblePosition >= kVisibleGlyphs) {
    return kBlankGlyph;
  }
  return glyphs_[position_ + visiblePosition];
}

bool ScrollSequence::advance(uint32_t now) {
  if (!active_) {
    return false;
  }
  position_++;
  nextFrameAt_ = now + kFrameIntervalMs;
  return position_ < frameCount_;
}

}  // namespace shelfclock
