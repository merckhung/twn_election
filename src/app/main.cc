// twn_election: interactive 3D map + live dashboard for Taiwan's 2026-11-28
// nine-in-one local elections (Vulkan 3D, Skia 2D).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "src/app/app.h"

namespace {

void Usage() {
  std::printf(
      "Usage: twn_election [flags]\n"
      "  --root=DIR          project directory containing data/ and assets/ (default: .)\n"
      "  --results=FILE      results JSON to watch (default: data/election/2026/results.json)\n"
      "  --simulate          run a synthetic, clearly-labelled counting-night DEMO\n"
      "  --sim_duration=S    seconds for the simulated count to complete (default 180)\n"
      "  --sim_progress=P    start the simulation at progress P in [0,1]\n"
      "  --seed=N            simulation seed\n"
      "  --focus=CODE        start zoomed into a region (county 63000, town 63000010, ...)\n"
      "  --mode=N            colouring: 0 leader, 1 counting progress, 2 turnout, 3 referendum\n"
      "  --lang=L            UI language: zh-TW (default, 繁體中文), ja (日本語), en (English)\n"
      "  --width=W --height=H\n"
      "  --headless          render offscreen (no window); use with --screenshot\n"
      "  --screenshot=FILE   headless: write a PNG and exit\n"
      "  --hover=CODE        headless: show a region as hovered\n"
      "  --yaw=DEG --pitch=DEG  initial camera angles\n"
      "  --font_dir=DIR      directory scanned for CJK fonts (default /usr/share/fonts)\n"
      "  --validation        enable VK_LAYER_KHRONOS_validation\n"
      "  --help_overlay      start with the controls overlay visible\n");
}

bool Flag(const char* arg, const char* name, std::string* value) {
  const size_t n = std::strlen(name);
  if (std::strncmp(arg, name, n) != 0) return false;
  if (arg[n] == '=') {
    *value = arg + n + 1;
    return true;
  }
  if (arg[n] == '\0') {
    *value = "1";
    return true;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  twn::app::AppOptions o;
  // `bazel run` starts in the runfiles tree; default to the workspace.
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) o.root = ws;
  for (int i = 1; i < argc; ++i) {
    std::string v;
    const char* a = argv[i];
    if (Flag(a, "--root", &v)) o.root = v;
    else if (Flag(a, "--results", &v)) o.results_path = v;
    else if (Flag(a, "--simulate", &v)) o.simulate = v != "0" && v != "false";
    else if (Flag(a, "--sim_duration", &v)) o.sim_duration_s = std::atof(v.c_str());
    else if (Flag(a, "--sim_progress", &v)) o.sim_progress = std::atof(v.c_str());
    else if (Flag(a, "--seed", &v)) o.seed = std::strtoull(v.c_str(), nullptr, 10);
    else if (Flag(a, "--focus", &v)) o.focus = v;
    else if (Flag(a, "--hover", &v)) o.hover = v;
    else if (Flag(a, "--mode", &v)) o.mode = std::atoi(v.c_str());
    else if (Flag(a, "--lang", &v)) {
      if (!twn::ui::ParseLang(v, &o.lang)) {
        std::fprintf(stderr, "unknown --lang=%s (use zh-TW, ja or en)\n", v.c_str());
        return 2;
      }
    }
    else if (Flag(a, "--width", &v)) o.width = std::atoi(v.c_str());
    else if (Flag(a, "--height", &v)) o.height = std::atoi(v.c_str());
    else if (Flag(a, "--headless", &v)) o.headless = v != "0";
    else if (Flag(a, "--screenshot", &v)) o.screenshot = v;
    else if (Flag(a, "--yaw", &v)) o.yaw_deg = static_cast<float>(std::atof(v.c_str()));
    else if (Flag(a, "--pitch", &v)) o.pitch_deg = static_cast<float>(std::atof(v.c_str()));
    else if (Flag(a, "--font_dir", &v)) o.font_dir = v;
    else if (Flag(a, "--validation", &v)) o.validation = v != "0";
    else if (Flag(a, "--help_overlay", &v)) o.help = true;
    else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      Usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown flag: %s\n", a);
      Usage();
      return 2;
    }
  }
  twn::app::App app;
  return app.Run(o);
}
