#include "src/geo/region_tree.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "src/geo/projection.h"
#include "src/geo/topojson.h"

namespace twn::geo {
namespace {

float SignedArea(const std::vector<glm::vec2>& ring) {
  double a = 0;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    a += static_cast<double>(ring[j].x) * ring[i].y - static_cast<double>(ring[i].x) * ring[j].y;
  }
  return static_cast<float>(a * 0.5);
}

bool RingContains(const std::vector<glm::vec2>& ring, glm::vec2 p) {
  bool inside = false;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    const glm::vec2& a = ring[i];
    const glm::vec2& b = ring[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

bool PolygonContains(const Polygon2D& poly, glm::vec2 p) {
  if (poly.rings.empty() || !RingContains(poly.rings[0], p)) return false;
  for (size_t i = 1; i < poly.rings.size(); ++i) {
    if (RingContains(poly.rings[i], p)) return false;
  }
  return true;
}

float SegmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 ab = b - a;
  const float len2 = ab.x * ab.x + ab.y * ab.y;
  float t = len2 > 0 ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  const glm::vec2 d = a + ab * t - p;
  return std::sqrt(d.x * d.x + d.y * d.y);
}

float DistanceToOutline(const Polygon2D& poly, glm::vec2 p) {
  float best = std::numeric_limits<float>::max();
  for (const auto& ring : poly.rings) {
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
      best = std::min(best, SegmentDistance(p, ring[j], ring[i]));
    }
  }
  return best;
}

// Approximates the pole of inaccessibility of the largest polygon with a
// coarse-to-fine grid search. Good enough for label placement.
glm::vec2 LabelPoint(const Region& r) {
  const Polygon2D* largest = nullptr;
  float largest_area = -1;
  for (const Polygon2D& poly : r.polygons) {
    const float a = std::fabs(SignedArea(poly.rings[0]));
    if (a > largest_area) {
      largest_area = a;
      largest = &poly;
    }
  }
  if (!largest) return r.center();
  glm::vec2 mn(std::numeric_limits<float>::max()), mx(std::numeric_limits<float>::lowest());
  for (const glm::vec2& p : largest->rings[0]) {
    mn = glm::min(mn, p);
    mx = glm::max(mx, p);
  }
  glm::vec2 best = (mn + mx) * 0.5f;
  float best_d = PolygonContains(*largest, best) ? DistanceToOutline(*largest, best) : -1.f;
  constexpr int kGrid = 9;
  glm::vec2 lo = mn, hi = mx;
  for (int iter = 0; iter < 3; ++iter) {
    const glm::vec2 step = (hi - lo) / static_cast<float>(kGrid - 1);
    for (int i = 0; i < kGrid; ++i) {
      for (int j = 0; j < kGrid; ++j) {
        const glm::vec2 p = lo + glm::vec2(step.x * i, step.y * j);
        if (!PolygonContains(*largest, p)) continue;
        const float d = DistanceToOutline(*largest, p);
        if (d > best_d) {
          best_d = d;
          best = p;
        }
      }
    }
    lo = glm::max(mn, best - step);
    hi = glm::min(mx, best + step);
  }
  return best;
}

std::string Get(const Feature& f, const char* key) {
  auto it = f.properties.find(key);
  return it == f.properties.end() ? std::string() : it->second;
}

// The atlas mixes 台/臺; official names of these counties use 臺.
std::string NormalizeName(std::string name) {
  static const std::string kTai = "台";
  static const std::string kTaiOfficial = "臺";
  if (name.compare(0, kTai.size(), kTai) == 0) name.replace(0, kTai.size(), kTaiOfficial);
  return name;
}

std::vector<Polygon2D> ProjectPolygons(const std::vector<Polygon>& polys,
                                       const std::string& county) {
  std::vector<Polygon2D> out;
  out.reserve(polys.size());
  for (const Polygon& poly : polys) {
    Polygon2D p;
    for (const Ring& ring : poly.rings) {
      std::vector<glm::vec2> r;
      r.reserve(ring.size());
      for (const LonLat& ll : ring) r.push_back(Projection::Project(ll, county));
      // Drop the closing duplicate point.
      if (r.size() > 1 && r.front() == r.back()) r.pop_back();
      if (r.size() < 3) continue;
      p.rings.push_back(std::move(r));
    }
    if (p.rings.empty()) continue;
    // Enforce winding: exterior CCW, holes CW.
    for (size_t i = 0; i < p.rings.size(); ++i) {
      const bool ccw = SignedArea(p.rings[i]) > 0;
      if ((i == 0) != ccw) std::reverse(p.rings[i].begin(), p.rings[i].end());
    }
    out.push_back(std::move(p));
  }
  return out;
}

}  // namespace

const char* LevelName(Level level) {
  switch (level) {
    case Level::kNation: return "nation";
    case Level::kCounty: return "county";
    case Level::kTown: return "town";
    case Level::kVillage: return "village";
  }
  return "?";
}

int RegionTree::AddRegion(Region r) {
  r.id = static_cast<int>(regions_.size());
  by_code_[r.code] = r.id;
  if (r.parent >= 0) regions_[r.parent].children.push_back(r.id);
  regions_.push_back(std::move(r));
  return regions_.back().id;
}

void RegionTree::Finalize(Region* r) {
  r->min = glm::vec2(std::numeric_limits<float>::max());
  r->max = glm::vec2(std::numeric_limits<float>::lowest());
  double area = 0;
  for (const Polygon2D& poly : r->polygons) {
    for (size_t i = 0; i < poly.rings.size(); ++i) {
      for (const glm::vec2& p : poly.rings[i]) {
        r->min = glm::min(r->min, p);
        r->max = glm::max(r->max, p);
      }
      area += SignedArea(poly.rings[i]);  // holes are CW -> negative
    }
  }
  r->area_km2 = static_cast<float>(area);
  r->label = LabelPoint(*r);
}

bool RegionTree::LoadTopoJson(std::string_view json, std::string* error) {
  Topology topo;
  if (!ParseTopoJson(json, {"counties", "towns", "villages"}, &topo, error)) return false;
  if (!topo.objects.count("counties") || !topo.objects.count("towns")) {
    *error = "TopoJSON must contain 'counties' and 'towns'";
    return false;
  }
  regions_.clear();
  by_code_.clear();

  Region nation;
  nation.level = Level::kNation;
  nation.code = "TW";
  nation.name_zh = "臺灣";
  nation.name_en = "Taiwan";
  AddRegion(std::move(nation));

  auto sorted = [](std::vector<Feature> v, const char* key) {
    std::sort(v.begin(), v.end(),
              [key](const Feature& a, const Feature& b) { return Get(a, key) < Get(b, key); });
    return v;
  };

  for (const Feature& f : sorted(topo.objects["counties"], "COUNTYCODE")) {
    Region r;
    r.level = Level::kCounty;
    r.code = Get(f, "COUNTYCODE");
    r.county_code = r.code;
    r.name_zh = NormalizeName(Get(f, "COUNTYNAME"));
    r.name_en = Get(f, "COUNTYENG");
    r.parent = 0;
    r.polygons = ProjectPolygons(f.polygons, r.code);
    AddRegion(std::move(r));
  }
  for (const Feature& f : sorted(topo.objects["towns"], "TOWNCODE")) {
    Region r;
    r.level = Level::kTown;
    r.code = Get(f, "TOWNCODE");
    r.county_code = Get(f, "COUNTYCODE");
    r.name_zh = NormalizeName(Get(f, "TOWNNAME"));
    r.name_en = Get(f, "TOWNENG");
    r.parent = FindByCode(r.county_code);
    if (r.parent < 0 || r.code.empty()) continue;
    r.polygons = ProjectPolygons(f.polygons, r.county_code);
    AddRegion(std::move(r));
  }
  if (topo.objects.count("villages")) {
    int unnamed = 0;
    for (const Feature& f : sorted(topo.objects["villages"], "VILLCODE")) {
      Region r;
      r.level = Level::kVillage;
      r.code = Get(f, "VILLCODE");
      r.county_code = Get(f, "COUNTYCODE");
      r.name_zh = Get(f, "VILLNAME");
      r.name_en = Get(f, "VILLENG");
      r.parent = FindByCode(Get(f, "TOWNCODE"));
      if (r.parent < 0) continue;
      if (r.code.empty() || by_code_.count(r.code)) {
        r.code = Get(f, "TOWNCODE") + "-x" + std::to_string(++unnamed);
      }
      r.polygons = ProjectPolygons(f.polygons, r.county_code);
      if (r.polygons.empty()) continue;
      AddRegion(std::move(r));
    }
  }
  for (Region& r : regions_) {
    if (r.level != Level::kNation) Finalize(&r);
  }
  Region& n = regions_[0];
  n.min = glm::vec2(std::numeric_limits<float>::max());
  n.max = glm::vec2(std::numeric_limits<float>::lowest());
  for (int c : n.children) {
    n.min = glm::min(n.min, regions_[c].min);
    n.max = glm::max(n.max, regions_[c].max);
    n.area_km2 += regions_[c].area_km2;
  }
  n.label = n.center();
  return true;
}

int RegionTree::FindByCode(std::string_view code) const {
  auto it = by_code_.find(std::string(code));
  return it == by_code_.end() ? -1 : it->second;
}

bool RegionTree::Contains(const Region& r, glm::vec2 p) {
  if (p.x < r.min.x || p.y < r.min.y || p.x > r.max.x || p.y > r.max.y) return false;
  for (const Polygon2D& poly : r.polygons) {
    if (PolygonContains(poly, p)) return true;
  }
  return false;
}

int RegionTree::ChildAt(int parent, glm::vec2 p) const {
  if (parent < 0 || parent >= size()) return -1;
  for (int c : regions_[parent].children) {
    if (Contains(regions_[c], p)) return c;
  }
  return -1;
}

std::vector<int> RegionTree::Path(int id) const {
  std::vector<int> path;
  for (int r = id; r >= 0; r = regions_[r].parent) path.push_back(r);
  std::reverse(path.begin(), path.end());
  return path;
}

int RegionTree::AncestorAt(int id, Level level) const {
  for (int r = id; r >= 0; r = regions_[r].parent) {
    if (regions_[r].level == level) return r;
  }
  return -1;
}

}  // namespace twn::geo
