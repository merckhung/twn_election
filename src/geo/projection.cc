#include "src/geo/projection.h"

#include <cmath>

namespace twn::geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kKmPerDegLat = 110.574;
const double kKmPerDegLon = 111.320 * std::cos(Projection::kLat0 * kPi / 180.0);

constexpr Inset kInsets[] = {
    // Kinmen County (金門縣): ~118.35E 24.45N -> western Taiwan Strait. Its
    // Wuqiu township (烏坵鄉, ~119.45E) already lies mid-strait and stays put.
    {"09020", {118.35, 24.45}, {119.00, 24.65}, 119.0, "09020060"},
    // Lienchiang County (連江縣, Matsu): ~119.95E 26.17N -> north of Taoyuan.
    {"09007", {119.95, 26.17}, {120.15, 25.80}, 180.0, ""},
};

}  // namespace

glm::vec2 Projection::ProjectRaw(const LonLat& p) {
  return glm::vec2(static_cast<float>((p.lon - kLon0) * kKmPerDegLon),
                   static_cast<float>((p.lat - kLat0) * kKmPerDegLat));
}

glm::vec2 Projection::Project(const LonLat& p, std::string_view county_code) {
  if (const Inset* inset = InsetFor(county_code); inset && p.lon < inset->max_lon) {
    LonLat q{p.lon - inset->anchor.lon + inset->placed_at.lon,
             p.lat - inset->anchor.lat + inset->placed_at.lat};
    return ProjectRaw(q);
  }
  return ProjectRaw(p);
}

const Inset* Projection::InsetFor(std::string_view county_code) {
  for (const Inset& inset : kInsets) {
    if (county_code == inset.county_code) return &inset;
  }
  return nullptr;
}

const Inset* Projection::insets(int* count) {
  *count = static_cast<int>(sizeof(kInsets) / sizeof(kInsets[0]));
  return kInsets;
}

}  // namespace twn::geo
