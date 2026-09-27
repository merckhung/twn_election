// Fonts and small text helpers on top of Skia.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

namespace twn::ui {

class Fonts {
 public:
  // Scans `font_dir` (recursively) for a Traditional-Chinese-capable face.
  bool Load(const std::string& font_dir, std::string* error);

  // Japanese text uses Japanese glyph forms when a JP face is installed.
  void SetJapanese(bool japanese);

  SkFont Regular(float size) const;
  SkFont Bold(float size) const;
  const std::string& family() const { return japanese_ && jp_regular_ ? jp_family_ : family_; }

 private:
  sk_sp<SkFontMgr> mgr_;
  sk_sp<SkTypeface> regular_, bold_;        // Traditional Chinese (default)
  sk_sp<SkTypeface> jp_regular_, jp_bold_;  // Japanese
  std::string family_, jp_family_;
  bool japanese_ = false;
};

enum class Align { kLeft, kCenter, kRight };

float TextWidth(const SkFont& font, std::string_view text);

// Draws UTF-8 text with baseline at y; returns the advance.
float DrawText(SkCanvas* c, std::string_view text, float x, float y, const SkFont& font,
               SkColor color, Align align = Align::kLeft);

// Returns `text` shortened with "…" to fit `max_width`.
std::string Ellipsize(const SkFont& font, std::string_view text, float max_width);

// Splits UTF-8 text into lines no wider than `width` (CJK breaks anywhere,
// Latin at spaces).
std::vector<std::string> WrapText(const SkFont& font, const std::string& text, float width);

// Rounded "chip" label; returns its width. `s` is the UI scale.
float ChipWidth(const Fonts* fonts, const std::string& text, float h);
float Chip(SkCanvas* c, const Fonts* fonts, const std::string& text, float x, float y, float h,
           uint32_t color, float s, bool outline = false);

// Formats 1234567 as "1,234,567".
std::string FormatThousands(int64_t v);

}  // namespace twn::ui
