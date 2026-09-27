#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

struct Style {
  vec4 color;   // rgb, a: alpha
  vec4 params;  // x: height (km), y: highlight 0..1, z: base z (km), w: unused
};
layout(std430, set = 0, binding = 1) readonly buffer Styles { Style styles[]; };

layout(push_constant) uniform Push {
  vec4 p;  // x: 1 = outline pass, y: z lift (km), z: alpha multiplier, w: outline brightness
} pc;

layout(location = 0) in vec3 in_pos;  // x, y (km), top flag
layout(location = 1) in vec3 in_normal;
layout(location = 2) in uint in_slot;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_color;
layout(location = 3) out float v_highlight;
layout(location = 4) out float v_top;

void main() {
  Style s = styles[in_slot];
  float z = s.params.z + in_pos.z * s.params.x + pc.p.y;
  v_world = vec3(in_pos.xy, z);
  v_normal = in_normal;
  v_color = vec4(s.color.rgb, s.color.a * pc.p.z);
  v_highlight = s.params.y;
  v_top = in_pos.z;
  gl_Position = u.view_proj * vec4(v_world, 1.0);
}
