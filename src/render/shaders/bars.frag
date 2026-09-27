#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_color;
layout(location = 3) in float v_highlight;
layout(location = 4) in float v_top;

layout(location = 0) out vec4 out_color;

void main() {
  vec3 n = normalize(v_normal);
  float diffuse = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  float ambient = u.light_dir.w + 0.1;
  vec3 color = v_color.rgb * (ambient + (1.0 - ambient) * diffuse);
  // Glossy cap on top of each bar.
  color += vec3(0.25) * step(0.5, n.z);
  color = mix(color, vec3(1.0), v_highlight * 0.3);
  color = ApplyFog(color, v_world);
  out_color = vec4(color * v_color.a, v_color.a);
}
