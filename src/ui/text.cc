#include "src/ui/text.h"

#include <vector>

#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/ports/SkFontMgr_directory.h"

namespace twn::ui {

bool Fonts::Load(const std::string& font_dir, std::string* error) {
  mgr_ = SkFontMgr_New_Custom_Directory(font_dir.c_str());
  if (!mgr_ || mgr_->countFamilies() == 0) {
    *error = "no fonts found under " + font_dir;
    return false;
  }
  const char* preferred[] = {"Noto Sans CJK TC", "Noto Sans TC",     "Noto Sans CJK JP",
                             "Noto Sans CJK SC", "Source Han Sans TC", "Source Han Sans",
                             "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "AR PL UMing TW",
                             "Droid Sans Fallback", "PingFang TC",     "Microsoft JhengHei"};
  for (const char* family : preferred) {
    sk_sp<SkTypeface> tf = mgr_->matchFamilyStyle(family, SkFontStyle::Normal());
    if (tf) {
      regular_ = tf;
      bold_ = mgr_->matchFamilyStyle(family, SkFontStyle::Bold());
      if (!bold_) bold_ = regular_;
      family_ = family;
      break;
    }
  }
  if (!regular_) {
    // Fall back to any face that can render "臺".
    const SkUnichar tai = 0x81FA;
    regular_ = mgr_->matchFamilyStyleCharacter(nullptr, SkFontStyle::Normal(), nullptr, 0, tai);
    if (regular_) {
      bold_ = mgr_->matchFamilyStyleCharacter(nullptr, SkFontStyle::Bold(), nullptr, 0, tai);
      if (!bold_) bold_ = regular_;
      SkString name;
      regular_->getFamilyName(&name);
      family_ = name.c_str();
    }
  }
  for (const char* family : {"Noto Sans CJK JP", "Noto Sans JP", "Source Han Sans JP",
                             "Source Han Sans", "IPAexGothic", "IPAGothic", "TakaoGothic",
                             "Hiragino Sans", "Yu Gothic"}) {
    if (sk_sp<SkTypeface> tf = mgr_->matchFamilyStyle(family, SkFontStyle::Normal())) {
      jp_regular_ = tf;
      jp_bold_ = mgr_->matchFamilyStyle(family, SkFontStyle::Bold());
      if (!jp_bold_) jp_bold_ = jp_regular_;
      jp_family_ = family;
      break;
    }
  }
  if (!regular_) {
    *error = "no font with Traditional Chinese glyphs found under " + font_dir +
             " (install fonts-noto-cjk or pass --font_dir)";
    return false;
  }
  return true;
}

void Fonts::SetJapanese(bool japanese) { japanese_ = japanese; }

SkFont Fonts::Regular(float size) const {
  SkFont f(japanese_ && jp_regular_ ? jp_regular_ : regular_, size);
  f.setEdging(SkFont::Edging::kAntiAlias);
  f.setSubpixel(true);
  return f;
}

SkFont Fonts::Bold(float size) const {
  const bool jp = japanese_ && jp_regular_;
  SkFont f(jp ? jp_bold_ : bold_, size);
  f.setEdging(SkFont::Edging::kAntiAlias);
  f.setSubpixel(true);
  if (jp ? jp_bold_ == jp_regular_ : bold_ == regular_) f.setEmbolden(true);
  return f;
}

float TextWidth(const SkFont& font, std::string_view text) {
  return font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
}

float DrawText(SkCanvas* c, std::string_view text, float x, float y, const SkFont& font,
               SkColor color, Align align) {
  const float w = TextWidth(font, text);
  if (align == Align::kCenter) x -= w * 0.5f;
  else if (align == Align::kRight) x -= w;
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(color);
  c->drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, p);
  return w;
}

std::string Ellipsize(const SkFont& font, std::string_view text, float max_width) {
  if (TextWidth(font, text) <= max_width) return std::string(text);
  static const std::string kEllipsis = "…";
  // Walk back UTF-8 code points until it fits.
  std::string s(text);
  while (!s.empty()) {
    size_t cut = s.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
    if (TextWidth(font, s + kEllipsis) <= max_width) return s + kEllipsis;
  }
  return kEllipsis;
}

std::string FormatThousands(int64_t v) {
  std::string digits = std::to_string(v < 0 ? -v : v);
  std::string out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return v < 0 ? "-" + out : out;
}

namespace {

SkPaint ChipFill(SkColor c) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  return p;
}

float LuminanceOf(uint32_t c) {
  return (0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF)) / 255.f;
}

}  // namespace

// Splits UTF-8 text into lines no wider than `width`. CJK characters may
// break anywhere; runs of Latin letters/digits break at spaces.
std::vector<std::string> WrapText(const SkFont& font, const std::string& text, float width) {
  std::vector<std::string> tokens;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = text[i];
    if (c < 0x80 && c != ' ') {
      size_t j = i;
      while (j < text.size() && static_cast<unsigned char>(text[j]) < 0x80 && text[j] != ' ') ++j;
      tokens.push_back(text.substr(i, j - i));
      i = j;
    } else {
      const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
      tokens.push_back(text.substr(i, n));
      i += n;
    }
  }
  std::vector<std::string> lines;
  std::string line;
  for (const std::string& tok : tokens) {
    if (!line.empty() && TextWidth(font, line + tok) > width) {
      while (!line.empty() && line.back() == ' ') line.pop_back();
      lines.push_back(line);
      line.clear();
      if (tok == " ") continue;
    }
    line += tok;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

float ChipWidth(const Fonts* fonts, const std::string& text, float h) {
  return TextWidth(fonts->Bold(h * 0.62f), text) + h * 0.8f;
}

// Draws a small rounded "chip" with text; returns its width.
float Chip(SkCanvas* c, const Fonts* fonts, const std::string& text, float x, float y, float h,
           uint32_t color, float s, bool outline) {
  const SkFont f = fonts->Bold(h * 0.62f);
  const float w = TextWidth(f, text) + h * 0.8f;
  const SkRect r = SkRect::MakeXYWH(x, y, w, h);
  if (outline) {
    SkPaint p = ChipFill(color);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.2f * s);
    c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.6f * s, 0.6f * s), h / 2, h / 2), p);
    DrawText(c, text, x + w / 2, y + h * 0.71f, f, color, Align::kCenter);
  } else {
    c->drawRRect(SkRRect::MakeRectXY(r, h / 2, h / 2), ChipFill(color));
    const SkColor tc = LuminanceOf(color) > 0.6f ? SK_ColorBLACK : SK_ColorWHITE;
    DrawText(c, text, x + w / 2, y + h * 0.71f, f, tc, Align::kCenter);
  }
  return w;
}

}  // namespace twn::ui
