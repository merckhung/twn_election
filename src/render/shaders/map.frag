#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(push_constant) uniform Push {
  vec4 p;
} pc;

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_color;
layout(location = 3) in float v_highlight;
layout(location = 4) in float v_top;

layout(location = 0) out vec4 out_color;

void main() {
  if (v_color.a < 0.004) discard;
  vec3 base = v_color.rgb;
  vec3 color;
  if (pc.p.x > 0.5) {
    // Outline pass: darker rim of the region colour, brightened on hover.
    color = mix(base * 0.35, vec3(1.0), clamp(pc.p.w + v_highlight * 0.8, 0.0, 1.0));
  } else {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u.light_dir.xyz);
    float diffuse = max(dot(n, l), 0.0);
    float ambient = u.light_dir.w;
    // Side walls get a subtle vertical gradient to read as "volume".
    float wall = 1.0 - abs(n.z);
    float grad = mix(1.0, 0.55 + 0.45 * v_top, wall);
    color = base * (ambient + (1.0 - ambient) * diffuse) * grad;
    // Hover highlight: lift towards white + warm rim.
    color = mix(color, vec3(1.0, 0.97, 0.9), v_highlight * 0.35);
  }
  color = ApplyFog(color, v_world);
  out_color = vec4(color * v_color.a, v_color.a);  // premultiplied
}
