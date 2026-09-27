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
      "  --no_music          disable the ambient background music\n"
      "  --sim_speed=X       simulation speed: 1 (real time), 2, 4, ... 4096 (default 64)\n"
      "  --sim_clock=HH:MM   start the simulated count at this time (16:00-23:00)\n"
      "  --sim_progress=P    start at fraction P of the evening (16:00 + P*7h)\n"
      "  --seed=N            simulation seed\n"
      "  --focus=CODE        start zoomed into a region (county 63000, town 63000010, ...)\n"
      "  --mode=N            colouring: 0 leader, 1 counting progress, 2 turnout, 3 referendum\n"
      "  --lang=L            UI language: zh-TW (default, 繁體中文), ja (日本語), en (English)\n"
      "  --width=W --height=H\n"
      "  --headless          render offscreen (no window); use with --screenshot\n"
      "  --screenshot=FILE   headless: write a PNG and exit\n"
      "  --record=DIR        headless: write a PNG frame sequence (see --record_seconds/--fps)\n"
      "  --record_seconds=S --fps=F\n"
      "  --db=FILE           SQLite database (default <root>/twn_election.db)\n"
      "  --news              fetch + classify news (data/news/feeds.json)\n"
      "  --news_config=FILE  alternative feeds config\n"
      "  --llm_base_url=URL  OpenAI-compatible endpoint (default https://api.openai.com/v1)\n"
      "  --llm_model=NAME    model name (default gpt-4o-mini); key from $OPENAI_API_KEY\n"
      "  --no_llm            classify with the offline keyword heuristic only\n"
      "  --mock_news         generate labelled mock news (default with --simulate --news)\n"
      "  --mock_news_rate=N  mock items per (simulated) hour (default 12)\n"
      "  --hover=CODE        headless: show a region as hovered\n"
      "  --yaw=DEG --pitch=DEG  initial camera angles\n"
      "  --font_dir=DIR      directory scanned for CJK fonts (default /usr/share/fonts)\n"
      "  --validation        enable VK_LAYER_KHRONOS_validation\n"
      "  --help_overlay      start with the controls overlay visible\n"
      "  --show_news         start with the news panel open (key N)\n"
      "  --chart=NAME        start in a secondary chart: trend|seats|margins|parties|grid\n"
      "  --pip=0             hide the picture-in-picture latest-news window (key I)\n"
      "  --chart_tour=S      headless recording: cycle map and charts every S seconds\n");
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
    else if (Flag(a, "--no_music", &v)) o.music = v == "0" || v == "false";
    else if (Flag(a, "--sim_speed", &v)) o.sim_speed = std::atof(v.c_str());
    else if (Flag(a, "--sim_clock", &v)) {
      int h = 0, mi = 0;
      if (std::sscanf(v.c_str(), "%d:%d", &h, &mi) != 2) {
        std::fprintf(stderr, "--sim_clock expects HH:MM\n");
        return 2;
      }
      o.sim_clock = (h - 16) * 60.0 + mi;
    }
    else if (Flag(a, "--record", &v)) o.record_dir = v;
    else if (Flag(a, "--record_seconds", &v)) o.record_seconds = std::atof(v.c_str());
    else if (Flag(a, "--fps", &v)) o.record_fps = std::atof(v.c_str());
    else if (Flag(a, "--db", &v)) o.db_path = v;
    else if (Flag(a, "--news_config", &v)) o.news_config = v;
    else if (Flag(a, "--news", &v)) o.news = v != "0";
    else if (Flag(a, "--llm_base_url", &v)) o.llm.base_url = v;
    else if (Flag(a, "--llm_model", &v)) o.llm.model = v;
    else if (Flag(a, "--no_llm", &v)) o.no_llm = v != "0";
    else if (Flag(a, "--mock_news_rate", &v)) o.mock_news_per_hour = std::atof(v.c_str());
    else if (Flag(a, "--mock_news", &v)) o.mock_news = v != "0";
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
    else if (Flag(a, "--show_news", &v)) o.show_news = true;
    else if (Flag(a, "--pip", &v)) o.pip = v != "0" && v != "false";
    else if (Flag(a, "--chart_tour", &v)) o.chart_tour = std::atof(v.c_str());
    else if (Flag(a, "--chart", &v)) {
      const char* names[] = {"map", "trend", "seats", "margins", "parties", "grid"};
      o.chart = -1;
      for (int k = 0; k < 6; ++k) {
        if (v == names[k]) o.chart = k;
      }
      if (o.chart < 0) {
        std::fprintf(stderr, "--chart: map|trend|seats|margins|parties|grid\n");
        return 2;
      }
    }
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
