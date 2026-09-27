#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) out vec3 v_world;

void main() {
  const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1),
                                 vec2(0, 1));
  vec2 c = corners[gl_VertexIndex];
  v_world = vec3(mix(u.ground.xy, u.ground.zw, c), -0.02);
  gl_Position = u.view_proj * vec4(v_world, 1.0);
}
