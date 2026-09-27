// Minimal TopoJSON decoder (Polygon / MultiPolygon geometries only).
//
// Spec: https://github.com/topojson/topojson-specification
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace twn::geo {

struct LonLat {
  double lon = 0;
  double lat = 0;
};

using Ring = std::vector<LonLat>;

// rings[0] is the exterior ring; any further rings are holes.
struct Polygon {
  std::vector<Ring> rings;
};

struct Feature {
  std::map<std::string, std::string> properties;
  std::vector<Polygon> polygons;
};

struct Topology {
  // Geometry collections keyed by object name ("counties", "towns", ...).
  std::map<std::string, std::vector<Feature>> objects;
};

// Parses `json` and decodes the named objects (all objects when empty).
// Returns false and fills `error` on malformed input.
bool ParseTopoJson(std::string_view json, const std::vector<std::string>& object_names,
                   Topology* out, std::string* error);

}  // namespace twn::geo
