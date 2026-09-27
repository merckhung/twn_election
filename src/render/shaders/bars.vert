#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 in_pos;  // unit box: xy in [-0.5, 0.5], z in [0, 1]
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 i_base;   // x, y, z0, width x
layout(location = 3) in vec4 i_color;  // rgb, alpha
layout(location = 4) in vec4 i_size;   // height, highlight, width y, unused

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_color;
layout(location = 3) out float v_highlight;
layout(location = 4) out float v_top;

void main() {
  v_world = vec3(i_base.xy + in_pos.xy * vec2(i_base.w, i_size.z), i_base.z + in_pos.z * i_size.x);
  v_normal = in_normal;
  v_color = i_color;
  v_highlight = i_size.y;
  v_top = in_pos.z;
  gl_Position = u.view_proj * vec4(v_world, 1.0);
}
