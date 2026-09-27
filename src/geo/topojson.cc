#include "src/geo/topojson.h"

#include <algorithm>
#include <cstdint>

#include "nlohmann/json.hpp"

namespace twn::geo {
namespace {

using nlohmann::json;

struct Transform {
  bool quantized = false;
  double sx = 1, sy = 1, tx = 0, ty = 0;
};

// Decodes all arcs into absolute lon/lat coordinates.
std::vector<Ring> DecodeArcs(const json& arcs, const Transform& t) {
  std::vector<Ring> out;
  out.reserve(arcs.size());
  for (const json& arc : arcs) {
    Ring ring;
    ring.reserve(arc.size());
    int64_t x = 0, y = 0;
    for (const json& p : arc) {
      if (t.quantized) {
        x += p[0].get<int64_t>();
        y += p[1].get<int64_t>();
        ring.push_back({x * t.sx + t.tx, y * t.sy + t.ty});
      } else {
        ring.push_back({p[0].get<double>(), p[1].get<double>()});
      }
    }
    out.push_back(std::move(ring));
  }
  return out;
}

bool BuildRing(const json& arc_indices, const std::vector<Ring>& arcs, Ring* ring,
               std::string* error) {
  for (const json& idx_json : arc_indices) {
    const int64_t idx = idx_json.get<int64_t>();
    const bool reversed = idx < 0;
    const size_t arc_index = static_cast<size_t>(reversed ? ~idx : idx);
    if (arc_index >= arcs.size()) {
      *error = "arc index out of range";
      return false;
    }
    const Ring& arc = arcs[arc_index];
    // Consecutive arcs share their junction point; skip the duplicate.
    const size_t skip = ring->empty() ? 0 : 1;
    if (reversed) {
      for (size_t i = skip; i < arc.size(); ++i) ring->push_back(arc[arc.size() - 1 - i]);
    } else {
      for (size_t i = skip; i < arc.size(); ++i) ring->push_back(arc[i]);
    }
  }
  return true;
}

bool BuildPolygon(const json& rings_json, const std::vector<Ring>& arcs, Polygon* poly,
                  std::string* error) {
  for (const json& ring_json : rings_json) {
    Ring ring;
    if (!BuildRing(ring_json, arcs, &ring, error)) return false;
    if (ring.size() >= 4) poly->rings.push_back(std::move(ring));
  }
  return true;
}

}  // namespace

bool ParseTopoJson(std::string_view text, const std::vector<std::string>& object_names,
                   Topology* out, std::string* error) {
  json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) {
    *error = "invalid JSON";
    return false;
  }
  if (doc.value("type", "") != "Topology") {
    *error = "not a TopoJSON Topology";
    return false;
  }
  Transform t;
  if (doc.contains("transform")) {
    const json& tr = doc["transform"];
    t.quantized = true;
    t.sx = tr["scale"][0].get<double>();
    t.sy = tr["scale"][1].get<double>();
    t.tx = tr["translate"][0].get<double>();
    t.ty = tr["translate"][1].get<double>();
  }
  const std::vector<Ring> arcs = DecodeArcs(doc["arcs"], t);

  for (const auto& [name, object] : doc["objects"].items()) {
    if (!object_names.empty() &&
        std::find(object_names.begin(), object_names.end(), name) == object_names.end()) {
      continue;
    }
    std::vector<Feature>& features = out->objects[name];
    const json& geometries = object.contains("geometries") ? object["geometries"] : json::array();
    for (const json& g : geometries) {
      Feature f;
      if (g.contains("properties")) {
        for (const auto& [k, v] : g["properties"].items()) {
          f.properties[k] = v.is_string() ? v.get<std::string>() : v.dump();
        }
      }
      const std::string type = g.value("type", "");
      if (type == "Polygon") {
        Polygon p;
        if (!BuildPolygon(g["arcs"], arcs, &p, error)) return false;
        if (!p.rings.empty()) f.polygons.push_back(std::move(p));
      } else if (type == "MultiPolygon") {
        for (const json& poly_json : g["arcs"]) {
          Polygon p;
          if (!BuildPolygon(poly_json, arcs, &p, error)) return false;
          if (!p.rings.empty()) f.polygons.push_back(std::move(p));
        }
      }
      features.push_back(std::move(f));
    }
  }
  return true;
}

}  // namespace twn::geo
