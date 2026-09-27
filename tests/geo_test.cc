#include <cmath>
#include <set>

#include "glm/geometric.hpp"
#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/geo/mesh_builder.h"
#include "src/geo/projection.h"
#include "src/geo/region_tree.h"
#include "src/geo/topojson.h"

namespace twn::geo {
namespace {

// A 2x1 quantized rectangle split into two unit squares that share the arc
// (1,0)-(1,1). Arc coordinates are delta-encoded.
constexpr char kTinyTopo[] = R"({
  "type": "Topology",
  "transform": {"scale": [1, 1], "translate": [0, 0]},
  "arcs": [
    [[1, 0], [0, 1]],
    [[1, 1], [-1, 0], [0, -1], [1, 0]],
    [[1, 0], [1, 0], [0, 1], [-1, 0]]
  ],
  "objects": {
    "counties": {"type": "GeometryCollection", "geometries": [
      {"type": "Polygon", "arcs": [[0, 1]], "properties": {"COUNTYCODE": "A"}},
      {"type": "MultiPolygon", "arcs": [[[2, -1]]], "properties": {"COUNTYCODE": "B"}}
    ]}
  }
})";

TEST(TopoJson, DecodesQuantizedDeltaArcs) {
  Topology topo;
  std::string error;
  ASSERT_TRUE(ParseTopoJson(kTinyTopo, {}, &topo, &error)) << error;
  const auto& counties = topo.objects["counties"];
  ASSERT_EQ(counties.size(), 2u);
  EXPECT_EQ(counties[0].properties.at("COUNTYCODE"), "A");
  // Square A: arc 0 (1,0)->(1,1) then arc 1 (1,1)->(0,1)->(0,0)->(1,0); the
  // junction point shared by consecutive arcs is not duplicated.
  const Ring& a = counties[0].polygons[0].rings[0];
  ASSERT_EQ(a.size(), 5u);
  EXPECT_DOUBLE_EQ(a[1].lon, 1);
  EXPECT_DOUBLE_EQ(a[1].lat, 1);
  EXPECT_DOUBLE_EQ(a[2].lon, 0);
  EXPECT_DOUBLE_EQ(a[3].lat, 0);
  // Square B: arc 2 then arc 0 reversed (~0 == -1): (1,1)->(1,0).
  ASSERT_EQ(counties[1].polygons.size(), 1u);
  const Ring& b = counties[1].polygons[0].rings[0];
  ASSERT_EQ(b.size(), 5u);
  EXPECT_DOUBLE_EQ(b[2].lon, 2);
  EXPECT_DOUBLE_EQ(b[2].lat, 1);
  EXPECT_DOUBLE_EQ(b.back().lon, 1);
  EXPECT_DOUBLE_EQ(b.back().lat, 0);
}

TEST(TopoJson, RejectsGarbage) {
  Topology topo;
  std::string error;
  EXPECT_FALSE(ParseTopoJson("{not json", {}, &topo, &error));
  EXPECT_FALSE(ParseTopoJson(R"({"type":"FeatureCollection"})", {}, &topo, &error));
}

TEST(Projection, KilometreScaleAroundTaiwan) {
  // Taipei Main Station to Kaohsiung Main Station is ~294 km great-circle.
  const glm::vec2 taipei = Projection::ProjectRaw({121.517, 25.048});
  const glm::vec2 kaohsiung = Projection::ProjectRaw({120.302, 22.639});
  EXPECT_NEAR(glm::length(taipei - kaohsiung), 294.0, 3.0);
}

TEST(Projection, InsetsMoveOutlyingIslandsOnly) {
  const LonLat kinmen{118.35, 24.45};
  EXPECT_NE(Projection::Project(kinmen, "09020"), Projection::ProjectRaw(kinmen));
  const LonLat wuqiu{119.45, 24.98};  // Kinmen County, kept in place
  EXPECT_EQ(Projection::Project(wuqiu, "09020"), Projection::ProjectRaw(wuqiu));
  const LonLat taipei{121.5, 25.0};
  EXPECT_EQ(Projection::Project(taipei, "63000"), Projection::ProjectRaw(taipei));
}

class TaiwanAtlasTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::string json;
    ASSERT_TRUE(election::ReadFile("data/map/villages-10t.json", &json));
    std::string error;
    tree_ = new RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
  }
  static void TearDownTestSuite() { delete tree_; }
  static RegionTree* tree_;
};

RegionTree* TaiwanAtlasTest::tree_ = nullptr;

TEST_F(TaiwanAtlasTest, Hierarchy) {
  const Region& nation = tree_->nation();
  EXPECT_EQ(nation.children.size(), 22u);
  int towns = 0, villages = 0;
  for (int c : nation.children) {
    EXPECT_EQ(tree_->region(c).level, Level::kCounty);
    towns += static_cast<int>(tree_->region(c).children.size());
    for (int t : tree_->region(c).children) {
      villages += static_cast<int>(tree_->region(t).children.size());
    }
  }
  EXPECT_EQ(towns, 368);
  EXPECT_GT(villages, 7500);
}

TEST_F(TaiwanAtlasTest, OfficialCountyNamesAndCodes) {
  const int taipei = tree_->FindByCode("63000");
  ASSERT_GE(taipei, 0);
  EXPECT_EQ(tree_->region(taipei).name_zh, "臺北市");
  const int taitung = tree_->FindByCode("10014");
  ASSERT_GE(taitung, 0);
  EXPECT_EQ(tree_->region(taitung).name_zh, "臺東縣");
}

TEST_F(TaiwanAtlasTest, AreasArePlausible) {
  // Taiwan's total land area is ~36,000 km^2.
  EXPECT_NEAR(tree_->nation().area_km2, 36000, 1500);
  const Region& taipei = tree_->region(tree_->FindByCode("63000"));
  EXPECT_NEAR(taipei.area_km2, 271.8, 10);
}

TEST_F(TaiwanAtlasTest, PickingFindsTheRightRegion) {
  const glm::vec2 p = Projection::Project({121.5436, 25.0264}, "63000");  // Da'an District
  const int county = tree_->ChildAt(0, p);
  ASSERT_GE(county, 0);
  EXPECT_EQ(tree_->region(county).code, "63000");
  const int town = tree_->ChildAt(county, p);
  ASSERT_GE(town, 0);
  EXPECT_EQ(tree_->region(town).name_zh, "大安區");
  EXPECT_TRUE(RegionTree::Contains(tree_->region(town), tree_->region(town).label));
  EXPECT_EQ(tree_->AncestorAt(town, Level::kCounty), county);
  EXPECT_EQ(tree_->Path(town).size(), 3u);
}

TEST_F(TaiwanAtlasTest, LabelPointsAreInside) {
  int outside = 0;
  for (int c : tree_->nation().children) {
    for (int t : tree_->region(c).children) {
      if (!RegionTree::Contains(tree_->region(t), tree_->region(t).label)) ++outside;
    }
  }
  EXPECT_EQ(outside, 0);
}

TEST_F(TaiwanAtlasTest, MeshTopAreaMatchesPolygonArea) {
  const Region& county = tree_->region(tree_->FindByCode("63000"));
  const MapMesh mesh = BuildMapMesh(*tree_, county.children);
  EXPECT_EQ(mesh.region_ids.size(), county.children.size());
  double top_area = 0;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const MapVertex& a = mesh.vertices[mesh.indices[i]];
    const MapVertex& b = mesh.vertices[mesh.indices[i + 1]];
    const MapVertex& c = mesh.vertices[mesh.indices[i + 2]];
    if (a.nz < 0.5f || b.nz < 0.5f || c.nz < 0.5f) continue;  // walls
    top_area += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
  }
  EXPECT_NEAR(top_area, county.area_km2, county.area_km2 * 0.01);
  EXPECT_FALSE(mesh.line_vertices.empty());
  EXPECT_EQ(mesh.line_vertices.size() % 2, 0u);
}

}  // namespace
}  // namespace twn::geo
