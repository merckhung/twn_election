#include "src/app/camera.h"

#include <algorithm>
#include <cmath>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/common.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"

namespace twn::app {
namespace {

constexpr float kPi = 3.14159265f;

float EaseInOut(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }

float LerpAngle(float a, float b, float t) {
  float d = std::fmod(b - a, 2 * kPi);
  if (d > kPi) d -= 2 * kPi;
  if (d < -kPi) d += 2 * kPi;
  return a + d * t;
}

}  // namespace

void Camera::SetPose(const CameraPose& p) {
  pose_ = p;
  anim_len_ = 0;
  anim_t_ = 0;
  Clamp();
}

void Camera::FlyTo(const CameraPose& to, float seconds) {
  from_ = pose_;
  to_ = to;
  anim_t_ = 0;
  anim_len_ = std::max(0.01f, seconds);
}

void Camera::Update(float dt) {
  if (!animating()) return;
  anim_t_ = std::min(anim_len_, anim_t_ + dt);
  const float t = EaseInOut(anim_t_ / anim_len_);
  pose_.target = from_.target + (to_.target - from_.target) * t;
  // Interpolate distance logarithmically so zooms feel uniform.
  pose_.distance = std::exp(std::log(from_.distance) +
                            (std::log(to_.distance) - std::log(from_.distance)) * t);
  pose_.yaw = LerpAngle(from_.yaw, to_.yaw, t);
  pose_.pitch = from_.pitch + (to_.pitch - from_.pitch) * t;
}

void Camera::Clamp() {
  pose_.pitch = std::clamp(pose_.pitch, kMinPitch, kMaxPitch);
  pose_.distance = std::clamp(pose_.distance, 0.4f, 2500.f);
  pose_.target = glm::clamp(pose_.target, glm::vec2(-600.f), glm::vec2(600.f));
}

glm::vec3 Camera::Eye() const {
  const float cp = std::cos(pose_.pitch), sp = std::sin(pose_.pitch);
  // Heading `yaw` measured clockwise from north; the eye sits behind the target.
  const glm::vec3 back(-std::sin(pose_.yaw) * cp, -std::cos(pose_.yaw) * cp, sp);
  return glm::vec3(pose_.target, 0.f) + back * pose_.distance;
}

glm::mat4 Camera::View() const {
  const glm::vec3 up(std::sin(pose_.yaw), std::cos(pose_.yaw), 0.f);
  return glm::lookAt(Eye(), glm::vec3(pose_.target, 0.f),
                     pose_.pitch > 1.49f ? up : glm::vec3(0, 0, 1));
}

glm::mat4 Camera::Projection() const {
  const float near_plane = std::max(0.05f, pose_.distance * 0.02f);
  const float far_plane = pose_.distance * 8.f + 1500.f;
  glm::mat4 p = glm::perspective(kFovY, width_ / std::max(1.f, height_), near_plane, far_plane);
  p[1][1] *= -1.f;  // Vulkan clip space has +y down.
  return p;
}

bool Camera::ScreenToGround(glm::vec2 s, float z, glm::vec2* out) const {
  const glm::mat4 inv = glm::inverse(ViewProj());
  const glm::vec2 ndc(s.x / width_ * 2.f - 1.f, s.y / height_ * 2.f - 1.f);
  glm::vec4 a = inv * glm::vec4(ndc, 0.f, 1.f);
  glm::vec4 b = inv * glm::vec4(ndc, 1.f, 1.f);
  a /= a.w;
  b /= b.w;
  const glm::vec3 dir = glm::vec3(b) - glm::vec3(a);
  if (std::fabs(dir.z) < 1e-6f) return false;
  const float t = (z - a.z) / dir.z;
  if (t < 0) return false;
  const glm::vec3 p = glm::vec3(a) + dir * t;
  *out = glm::vec2(p.x, p.y);
  return true;
}

bool Camera::WorldToScreen(glm::vec3 w, glm::vec2* out) const {
  const glm::vec4 c = ViewProj() * glm::vec4(w, 1.f);
  if (c.w <= 1e-4f) return false;
  *out = glm::vec2((c.x / c.w * 0.5f + 0.5f) * width_, (c.y / c.w * 0.5f + 0.5f) * height_);
  return true;
}

CameraPose Camera::Frame(glm::vec2 min, glm::vec2 max, float pitch, float yaw,
                         float left_margin_px, float right_margin_px) const {
  CameraPose p;
  p.pitch = pitch;
  p.yaw = yaw;
  const glm::vec2 size = glm::max(max - min, glm::vec2(0.5f));
  const float usable_w = std::max(200.f, width_ - left_margin_px - right_margin_px);
  const float aspect = usable_w / height_;
  const float fov_x = 2 * std::atan(std::tan(kFovY / 2) * aspect);
  // Rotate extents into the view heading.
  const float c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw));
  const float ext_x = size.x * c + size.y * s;
  const float ext_y = (size.x * s + size.y * c) * std::sin(pitch) + 0.15f * size.y;
  const float d_x = ext_x * 0.5f / std::tan(fov_x / 2);
  const float d_y = ext_y * 0.5f / std::tan(kFovY / 2);
  p.distance = std::max(d_x, d_y) * 1.12f;
  // Shift the target (in view space) so the box is centred in the area
  // between the dashboard panels.
  const float shift_px = (right_margin_px - left_margin_px) * 0.5f;
  const float km_per_px = 2 * p.distance * std::tan(fov_x / 2) / usable_w;
  const glm::vec2 right_dir(std::cos(yaw), -std::sin(yaw));
  p.target = (min + max) * 0.5f + right_dir * (shift_px * km_per_px);
  return p;
}

void Camera::PanScreen(glm::vec2 from_px, glm::vec2 to_px) {
  glm::vec2 a, b;
  if (!ScreenToGround(from_px, 0, &a) || !ScreenToGround(to_px, 0, &b)) return;
  pose_.target += a - b;
  anim_len_ = 0;
  Clamp();
}

void Camera::Orbit(float dyaw, float dpitch) {
  pose_.yaw += dyaw;
  pose_.pitch += dpitch;
  anim_len_ = 0;
  Clamp();
}

void Camera::ZoomAt(glm::vec2 screen, float factor) {
  glm::vec2 before;
  const bool hit = ScreenToGround(screen, 0, &before);
  pose_.distance *= factor;
  anim_len_ = 0;
  Clamp();
  glm::vec2 after;
  if (hit && ScreenToGround(screen, 0, &after)) pose_.target += before - after;
  Clamp();
}

void Camera::PanKm(glm::vec2 d) {
  const glm::vec2 fwd(std::sin(pose_.yaw), std::cos(pose_.yaw));
  const glm::vec2 right(std::cos(pose_.yaw), -std::sin(pose_.yaw));
  pose_.target += (right * d.x + fwd * d.y) * pose_.distance;
  anim_len_ = 0;
  Clamp();
}

}  // namespace twn::app
