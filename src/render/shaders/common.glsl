// Shared declarations (textually included by the build: see BUILD.bazel).
layout(set = 0, binding = 0) uniform Frame {
  mat4 view_proj;
  vec4 eye;        // xyz: camera position (km), w: time (s)
  vec4 light_dir;  // xyz: direction *towards* the light, w: ambient
  vec4 ground;     // xy: min, zw: max of the sea plane (km)
  vec4 fog;        // rgb: fog colour, w: density
  vec4 sea;        // rgb: sea colour, w: grid spacing (km)
} u;

vec3 ApplyFog(vec3 color, vec3 world) {
  float d = length(world - u.eye.xyz);
  float f = 1.0 - exp(-pow(d * u.fog.w, 1.6));
  return mix(color, u.fog.rgb, clamp(f, 0.0, 0.85));
}
