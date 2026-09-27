#include "src/ui/text.h"

#include <vector>

#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
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

}  // namespace twn::ui
