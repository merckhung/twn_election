// Administrative hierarchy of Taiwan: nation -> 22 counties/cities ->
// 368 towns/districts -> ~7,700 villages/boroughs, with projected geometry.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "glm/common.hpp"
#include "glm/vec2.hpp"

namespace twn::geo {

enum class Level : uint8_t { kNation = 0, kCounty = 1, kTown = 2, kVillage = 3 };

const char* LevelName(Level level);

// A polygon in projected km; rings[0] is the exterior (CCW), others holes (CW).
struct Polygon2D {
  std::vector<std::vector<glm::vec2>> rings;
};

struct Region {
  int id = -1;
  Level level = Level::kNation;
  std::string code;         // "TW", COUNTYCODE, TOWNCODE or VILLCODE
  std::string county_code;  // owning county ("" for the nation)
  std::string name_zh;
  std::string name_en;
  int parent = -1;
  std::vector<int> children;
  std::vector<Polygon2D> polygons;
  glm::vec2 min{0}, max{0};  // bounding box (km)
  glm::vec2 label{0};        // good interior point for labels / markers
  float area_km2 = 0;

  glm::vec2 center() const { return (min + max) * 0.5f; }
  glm::vec2 size() const { return max - min; }
};

class RegionTree {
 public:
  // Loads a taiwan-atlas style TopoJSON containing "counties", "towns" and
  // optionally "villages" geometry collections.
  bool LoadTopoJson(std::string_view json, std::string* error);

  int size() const { return static_cast<int>(regions_.size()); }
  const Region& region(int id) const { return regions_[id]; }
  const Region& nation() const { return regions_[0]; }

  // Returns the region id for a code, or -1.
  int FindByCode(std::string_view code) const;

  // Returns the child of `parent` whose area contains `p`, or -1.
  int ChildAt(int parent, glm::vec2 p) const;

  // Ids from the nation down to `id` (inclusive).
  std::vector<int> Path(int id) const;

  // Returns the ancestor of `id` at `level` (or `id` itself), or -1.
  int AncestorAt(int id, Level level) const;

  static bool Contains(const Region& r, glm::vec2 p);

 private:
  int AddRegion(Region r);
  void Finalize(Region* r);

  std::vector<Region> regions_;
  std::unordered_map<std::string, int> by_code_;
};

}  // namespace twn::geo
