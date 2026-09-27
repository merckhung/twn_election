#include "src/geo/mesh_builder.h"

#include <array>
#include <cmath>
#include <limits>

#include "earcut.hpp"

namespace twn::geo {

std::vector<uint32_t> Triangulate(const Polygon2D& poly) {
  std::vector<std::vector<std::array<float, 2>>> rings;
  rings.reserve(poly.rings.size());
  for (const auto& ring : poly.rings) {
    std::vector<std::array<float, 2>> r;
    r.reserve(ring.size());
    for (const glm::vec2& p : ring) r.push_back({p.x, p.y});
    rings.push_back(std::move(r));
  }
  return mapbox::earcut<uint32_t>(rings);
}

MapMesh BuildMapMesh(const RegionTree& tree, std::span<const int> region_ids) {
  MapMesh mesh;
  mesh.region_ids.assign(region_ids.begin(), region_ids.end());
  mesh.min = glm::vec2(std::numeric_limits<float>::max());
  mesh.max = glm::vec2(std::numeric_limits<float>::lowest());

  for (uint32_t slot = 0; slot < region_ids.size(); ++slot) {
    const Region& r = tree.region(region_ids[slot]);
    mesh.min = glm::min(mesh.min, r.min);
    mesh.max = glm::max(mesh.max, r.max);
    for (const Polygon2D& poly : r.polygons) {
      // Top face.
      const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
      for (const auto& ring : poly.rings) {
        for (const glm::vec2& p : ring) {
          mesh.vertices.push_back({p.x, p.y, 1.f, 0.f, 0.f, 1.f, slot});
        }
      }
      for (uint32_t idx : Triangulate(poly)) mesh.indices.push_back(base + idx);

      // Walls + outlines. Exterior rings are CCW and holes CW, so the outward
      // normal of edge a->b is always (dy, -dx).
      for (const auto& ring : poly.rings) {
        for (size_t i = 0; i < ring.size(); ++i) {
          const glm::vec2 a = ring[i];
          const glm::vec2 b = ring[(i + 1) % ring.size()];
          const glm::vec2 d = b - a;
          const float len = std::sqrt(d.x * d.x + d.y * d.y);
          if (len <= 0) continue;
          const float nx = d.y / len, ny = -d.x / len;
          const uint32_t w = static_cast<uint32_t>(mesh.vertices.size());
          mesh.vertices.push_back({a.x, a.y, 0.f, nx, ny, 0.f, slot});
          mesh.vertices.push_back({b.x, b.y, 0.f, nx, ny, 0.f, slot});
          mesh.vertices.push_back({b.x, b.y, 1.f, nx, ny, 0.f, slot});
          mesh.vertices.push_back({a.x, a.y, 1.f, nx, ny, 0.f, slot});
          mesh.indices.insert(mesh.indices.end(), {w, w + 1, w + 2, w, w + 2, w + 3});

          mesh.line_vertices.push_back({a.x, a.y, 1.f, 0.f, 0.f, 1.f, slot});
          mesh.line_vertices.push_back({b.x, b.y, 1.f, 0.f, 0.f, 1.f, slot});
        }
      }
    }
  }
  return mesh;
}

}  // namespace twn::geo
