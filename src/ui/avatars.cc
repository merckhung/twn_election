#include "src/ui/avatars.h"

#include <filesystem>

#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkGradient.h"
#include "stb_image.h"

namespace twn::ui {
namespace {

sk_sp<SkImage> LoadImage(const std::string& path) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
  if (!data) return nullptr;
  SkPixmap pm(SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType), data,
              static_cast<size_t>(w) * 4);
  sk_sp<SkImage> img = SkImages::RasterFromPixmapCopy(pm);
  stbi_image_free(data);
  return img;
}

// First UTF-8 code point of `s`.
std::string FirstChar(const std::string& s) {
  if (s.empty()) return "?";
  const unsigned char c = s[0];
  size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
  return s.substr(0, std::min(n, s.size()));
}

}  // namespace

sk_sp<SkImage> Avatars::Photo(const election::Candidate& cand) {
  auto it = photos_.find(cand.id);
  if (it != photos_.end()) return it->second;
  sk_sp<SkImage> img;
  std::vector<std::string> paths;
  if (!cand.photo.empty()) paths.push_back(root_ + "/" + cand.photo);
  paths.push_back(root_ + "/assets/photos/" + cand.id + ".jpg");
  paths.push_back(root_ + "/assets/photos/" + cand.id + ".png");
  for (const std::string& p : paths) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec) && (img = LoadImage(p))) break;
  }
  photos_[cand.id] = img;
  return img;
}

bool Avatars::HasPhoto(const election::Candidate& cand) { return Photo(cand) != nullptr; }

int Avatars::photo_count() const {
  int n = 0;
  for (const auto& [id, img] : photos_) n += img != nullptr;
  return n;
}

void Avatars::Draw(SkCanvas* c, const election::Candidate& cand, const election::Party& party,
                   float x, float y, float size, bool ring) {
  const SkRect r = SkRect::MakeXYWH(x, y, size, size);
  const SkColor color = party.color;
  c->save();
  SkPath clip = SkPath::Oval(r);
  c->clipPath(clip, true);
  if (sk_sp<SkImage> img = Photo(cand)) {
    // Centre-crop to a square, biased towards the top (faces).
    const float iw = static_cast<float>(img->width()), ih = static_cast<float>(img->height());
    const float side = std::min(iw, ih);
    const SkRect src = SkRect::MakeXYWH((iw - side) * 0.5f, (ih - side) * 0.15f, side, side);
    SkPaint p;
    p.setAntiAlias(true);
    c->drawImageRect(img, src, r, SkSamplingOptions(SkCubicResampler::Mitchell()), &p,
                     SkCanvas::kFast_SrcRectConstraint);
  } else {
    const SkColor4f top = SkColor4f::FromColor(color);
    const SkColor4f bottom = {top.fR * 0.55f, top.fG * 0.55f, top.fB * 0.55f, 1.f};
    const SkColor4f colors[2] = {top, bottom};
    const SkPoint pts[2] = {{x, y}, {x, y + size}};
    SkPaint p;
    p.setAntiAlias(true);
    p.setShader(SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors(colors, {}, SkTileMode::kClamp), {})));
    c->drawRect(r, p);
    const SkFont f = fonts_->Bold(size * 0.46f);
    DrawText(c, FirstChar(cand.name_zh), x + size * 0.5f, y + size * 0.66f, f, SK_ColorWHITE,
             Align::kCenter);
  }
  c->restore();
  if (ring) {
    SkPaint p;
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(std::max(1.5f, size * 0.05f));
    p.setColor(color);
    c->drawOval(r.makeInset(p.getStrokeWidth() * 0.5f, p.getStrokeWidth() * 0.5f), p);
  }
}

}  // namespace twn::ui
