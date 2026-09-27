// Orbit camera over the map plane (z up, km units) with grab-to-pan,
// zoom-to-cursor, orbit/tilt and smooth animated "fly to" transitions.
#pragma once

#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

namespace twn::app {

struct CameraPose {
  glm::vec2 target{0.f};   // look-at point on the ground (km)
  float distance = 600.f;  // eye distance from target (km)
  float yaw = 0.f;         // radians, 0 = looking north
  float pitch = 0.9f;      // radians above the horizon (0.2 .. 1.5)
};

class Camera {
 public:
  static constexpr float kFovY = 0.6108652f;  // 35 degrees
  static constexpr float kMinPitch = 0.25f;
  static constexpr float kMaxPitch = 1.5f;

  void SetViewport(float width, float height) {
    width_ = width;
    height_ = height;
  }

  const CameraPose& pose() const { return pose_; }
  void SetPose(const CameraPose& p);

  // Starts an eased transition to `to` lasting `seconds`.
  void FlyTo(const CameraPose& to, float seconds);
  bool animating() const { return anim_t_ < anim_len_; }
  void Update(float dt);

  // Pose that frames the box [min, max] with the given tilt/yaw, leaving
  // `left_margin_px` / `right_margin_px` pixels free for the dashboard panels.
  CameraPose Frame(glm::vec2 min, glm::vec2 max, float pitch, float yaw, float left_margin_px,
                   float right_margin_px) const;

  glm::vec3 Eye() const;
  glm::mat4 View() const;
  glm::mat4 Projection() const;
  glm::mat4 ViewProj() const { return Projection() * View(); }

  // Screen (px) -> ground point at height z. Returns false if the ray misses.
  bool ScreenToGround(glm::vec2 screen, float z, glm::vec2* out) const;
  // World -> screen (px). Returns false if behind the camera.
  bool WorldToScreen(glm::vec3 world, glm::vec2* out) const;

  // Interaction.
  void PanScreen(glm::vec2 from_px, glm::vec2 to_px);  // keeps the grabbed point under cursor
  void Orbit(float dyaw, float dpitch);
  void ZoomAt(glm::vec2 screen_px, float factor);
  void PanKm(glm::vec2 delta_screen_dir);  // keyboard pan, relative to view heading

 private:
  void Clamp();

  float width_ = 1600, height_ = 900;
  CameraPose pose_;
  CameraPose from_, to_;
  float anim_t_ = 0, anim_len_ = 0;
};

}  // namespace twn::app
