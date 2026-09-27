#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 0) out vec4 out_color;

float GridLine(vec2 p, float spacing) {
  vec2 g = abs(fract(p / spacing - 0.5) - 0.5) / fwidth(p / spacing);
  return 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);
}

void main() {
  vec3 color = u.sea.rgb;
  // Soft radial glow around the island.
  float r = length(v_world.xy) / 260.0;
  color *= 1.15 - 0.35 * clamp(r, 0.0, 1.0);
  float fine = GridLine(v_world.xy, u.sea.w);
  float coarse = GridLine(v_world.xy, u.sea.w * 5.0);
  color += vec3(0.05, 0.09, 0.13) * fine + vec3(0.07, 0.12, 0.17) * coarse;
  color = ApplyFog(color, v_world);
  out_color = vec4(color, 1.0);
}
