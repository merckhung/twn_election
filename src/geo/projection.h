// Map projection: lon/lat -> local planar kilometres (x east, y north).
//
// Taiwan spans ~4 degrees, so an equirectangular projection centred on the
// island is accurate to well under 1% here. The outlying Kinmen and Lienchiang
// (Matsu) counties sit close to mainland China; like most Taiwanese election
// maps we draw them as insets moved nearer to the main island.
#pragma once

#include <string_view>

#include "glm/vec2.hpp"
#include "src/geo/topojson.h"

namespace twn::geo {

struct Inset {
  const char* county_code;
  LonLat anchor;         // real-world reference point of the inset
  LonLat placed_at;      // where that point is drawn
  double max_lon;        // only points west of this are moved
  const char* kept_town;  // town left at its true position ("" = none)
};

class Projection {
 public:
  static constexpr double kLon0 = 120.95;
  static constexpr double kLat0 = 23.75;

  // Projects a point belonging to `county_code` (insets are applied per county).
  static glm::vec2 Project(const LonLat& p, std::string_view county_code);

  // Plain projection without inset offsets.
  static glm::vec2 ProjectRaw(const LonLat& p);

  // Returns the inset for a county, or nullptr.
  static const Inset* InsetFor(std::string_view county_code);

  static const Inset* insets(int* count);
};

}  // namespace twn::geo
