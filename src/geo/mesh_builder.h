// Builds GPU-ready extruded prism meshes (top faces, side walls, outlines)
// for a set of regions. Heights and colours are NOT baked in: every vertex
// carries a "slot" (index into the region list) and a 0/1 "top" flag, and the
// vertex shader looks up per-slot height/colour from a storage buffer. That
// lets the app animate extrusion and recolour regions without rebuilding.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "glm/common.hpp"
#include "glm/vec2.hpp"
#include "src/geo/region_tree.h"

namespace twn::geo {

struct MapVertex {
  float x, y;        // projected position (km)
  float top;         // 0 = ground, 1 = extruded top
  float nx, ny, nz;  // surface normal
  uint32_t slot;     // index into the region list the mesh was built from
};
static_assert(sizeof(MapVertex) == 28);

struct MapMesh {
  std::vector<MapVertex> vertices;  // triangles (tops + walls)
  std::vector<uint32_t> indices;
  std::vector<MapVertex> line_vertices;  // line list of region outlines (top)
  std::vector<int> region_ids;           // slot -> region id
  glm::vec2 min{0}, max{0};
};

MapMesh BuildMapMesh(const RegionTree& tree, std::span<const int> region_ids);

// Triangulates a polygon (exterior + holes). Returns indices into the
// concatenated ring vertices.
std::vector<uint32_t> Triangulate(const Polygon2D& poly);

}  // namespace twn::geo
