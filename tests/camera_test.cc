#include <cmath>

#include "glm/geometric.hpp"
#include "gtest/gtest.h"
#include "src/app/camera.h"

namespace twn::app {
namespace {

TEST(Camera, ScreenGroundRoundTrip) {
  Camera cam;
  cam.SetViewport(1600, 900);
  CameraPose p;
  p.target = {10, -20};
  p.distance = 300;
  p.yaw = 0.4f;
  p.pitch = 0.8f;
  cam.SetPose(p);
  for (glm::vec2 s : {glm::vec2(800, 450), glm::vec2(100, 700), glm::vec2(1500, 300)}) {
    glm::vec2 g, back;
    ASSERT_TRUE(cam.ScreenToGround(s, 0, &g));
    ASSERT_TRUE(cam.WorldToScreen(glm::vec3(g, 0), &back));
    EXPECT_NEAR(back.x, s.x, 0.5f);
    EXPECT_NEAR(back.y, s.y, 0.5f);
  }
  glm::vec2 centre;
  ASSERT_TRUE(cam.ScreenToGround({800, 450}, 0, &centre));
  EXPECT_NEAR(centre.x, 10, 0.5f);
  EXPECT_NEAR(centre.y, -20, 0.5f);
}

TEST(Camera, NorthIsUpAtZeroYaw) {
  Camera cam;
  cam.SetViewport(1000, 1000);
  CameraPose p;
  p.distance = 100;
  p.pitch = 1.2f;
  cam.SetPose(p);
  glm::vec2 north, east;
  ASSERT_TRUE(cam.WorldToScreen({0, 10, 0}, &north));
  ASSERT_TRUE(cam.WorldToScreen({10, 0, 0}, &east));
  EXPECT_LT(north.y, 500);  // north is towards the top of the screen
  EXPECT_GT(east.x, 500);   // east is to the right
}

TEST(Camera, ZoomKeepsPointUnderCursor) {
  Camera cam;
  cam.SetViewport(1600, 900);
  cam.SetPose(CameraPose{});
  const glm::vec2 cursor(400, 300);
  glm::vec2 before, after;
  ASSERT_TRUE(cam.ScreenToGround(cursor, 0, &before));
  cam.ZoomAt(cursor, 0.5f);
  ASSERT_TRUE(cam.ScreenToGround(cursor, 0, &after));
  EXPECT_NEAR(glm::length(before - after), 0, 0.5f);
  EXPECT_NEAR(cam.pose().distance, 300, 1);
}

TEST(Camera, PanDragsTheGround) {
  Camera cam;
  cam.SetViewport(1600, 900);
  cam.SetPose(CameraPose{});
  glm::vec2 grabbed;
  ASSERT_TRUE(cam.ScreenToGround({700, 400}, 0, &grabbed));
  cam.PanScreen({700, 400}, {900, 500});
  glm::vec2 now;
  ASSERT_TRUE(cam.ScreenToGround({900, 500}, 0, &now));
  EXPECT_NEAR(glm::length(grabbed - now), 0, 0.5f);
}

TEST(Camera, FrameFitsBoxAndAnimates) {
  Camera cam;
  cam.SetViewport(1600, 900);
  cam.SetPose(CameraPose{});
  const CameraPose target = cam.Frame({-10, -10}, {10, 10}, 0.9f, 0.f, 0.f, 0.f);
  EXPECT_NEAR(target.target.x, 0, 1e-3);
  EXPECT_LT(target.distance, 200);
  cam.FlyTo(target, 1.0f);
  EXPECT_TRUE(cam.animating());
  cam.Update(0.5f);
  EXPECT_TRUE(cam.animating());
  cam.Update(0.6f);
  EXPECT_FALSE(cam.animating());
  EXPECT_NEAR(cam.pose().distance, target.distance, 1e-3);
}

}  // namespace
}  // namespace twn::app
