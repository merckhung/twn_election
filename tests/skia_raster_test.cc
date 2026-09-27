// Smoke test for the Skia overlay path: CPU raster + FreeType CJK text.
#include <cstdlib>
#include <vector>

#include "gtest/gtest.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "src/ui/text.h"

namespace twn::ui {
namespace {

TEST(Skia, DrawsShapesAndChineseText) {
  const int w = 200, h = 60;
  std::vector<uint32_t> pixels(w * h, 0);
  auto canvas = SkCanvas::MakeRasterDirect(SkImageInfo::MakeN32Premul(w, h), pixels.data(), w * 4);
  ASSERT_TRUE(canvas);
  SkPaint p;
  p.setColor(SK_ColorRED);
  canvas->drawRect(SkRect::MakeXYWH(0, 0, 10, 10), p);
  EXPECT_EQ(pixels[5 * w + 5], SkPreMultiplyColor(SK_ColorRED));

  Fonts fonts;
  std::string error;
  const char* dir = std::getenv("TWN_FONT_DIR");
  if (!fonts.Load(dir ? dir : "/usr/share/fonts", &error)) {
    GTEST_SKIP() << "no CJK font installed: " << error;
  }
  const SkFont font = fonts.Bold(32);
  EXPECT_GT(TextWidth(font, "臺灣"), 40.f);
  DrawText(canvas.get(), "臺灣", 20, 45, font, SK_ColorWHITE);
  int lit = 0;
  for (int y = 10; y < h; ++y) {
    for (int x = 20; x < 100; ++x) lit += (pixels[y * w + x] >> 24) > 0;
  }
  EXPECT_GT(lit, 100);
  EXPECT_EQ(FormatThousands(1234567), "1,234,567");
  EXPECT_EQ(Ellipsize(font, "臺北市長選舉開票結果", 60).find("…") != std::string::npos, true);
}

}  // namespace
}  // namespace twn::ui
