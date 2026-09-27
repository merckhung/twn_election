# twn_election — 臺灣 2026 九合一選舉 3D 地圖 · 3D Taiwan election map

An interactive **3D map of Taiwan** with a live **vote dashboard** for the
**2026-11-28 nine-in-one local elections** (115年地方公職人員選舉). It is written in
C++20 and built with **Bazel (bzlmod only)**. 3D rendering uses **Vulkan**, and the
2D dashboard is drawn with **Skia**. The UI is available in **繁體中文 (default)**,
**日本語** and **English**.

![Nation view](docs/screenshots/01_nation_preelection.png)

| | |
|---|---|
| ![Counting simulation](docs/screenshots/03_nation_simulation.png) | ![Tainan](docs/screenshots/04_tainan_simulation.png) |
| ![Village level, Japanese UI](docs/screenshots/05_village_ja.png) | ![Miaoli, English UI](docs/screenshots/06_miaoli_en.png) |

> The coloured "counting" screenshots use the built-in **simulation**
> (`--simulate`). Its numbers are synthetic, random and party-blind. They are
> **not** results or forecasts, and the UI marks them "模擬資料 SIMULATION" at all times.
> Before election day the app shows the real pre-election state: countdown,
> incumbents and the registered candidates.

## Features

- **3D Taiwan map.** Every county, township/district and village/borough is an extruded,
  lit prism rendered with Vulkan (4× MSAA, fog, sea grid). Kinmen and Matsu are
  drawn as insets, as on CEC maps.
- **Navigation.** Grab-to-pan, zoom-to-cursor, orbit and tilt, and animated fly-to.
  - Click to drill down: **Taiwan → county/city (22) → township/district (368) →
    village/borough (~7,700)**.
  - Esc, Backspace or right-click goes back up. Clicking a neighbouring area jumps straight to it.
- **Dashboard (Skia).**
  - Countdown to election day and the counting status.
  - Seats by party: incumbents before the vote, then leading and elected.
  - All **22 races with their 81 candidates**: portraits, party chips, "backed by"
    endorsements for independents, and incumbent / term-limited tags.
  - Votes, shares and progress for any region you drill into.
  - Tooltips, a legend, the 9 offices on the same ballot, and the same-day
    **national referendum (case 22, nuclear power)** with its 25%-of-electorate threshold.
- **Party-aware colouring.**
  - Before counting, each region is tinted by the incumbent's party.
  - During counting, colour shows the leading candidate's party and brightness shows the margin.
  - **3D vote bars** show the top 3 candidates in every region.
  - Other colouring modes: counting progress, turnout, and referendum agree/disagree.
- **Live data.** The app watches a results JSON file and hot-reloads it whenever it
  changes (see [Results feed](#results-feed)). Feeds may report at any granularity:
  missing levels are aggregated up the village → township → county hierarchy.
- **Headless mode.** Renders offscreen, with no display or GPU needed (Mesa lavapipe
  works), and writes PNG screenshots for CI.

## Data

| What | Where | Source |
|---|---|---|
| 81 mayor/magistrate candidates, parties, incumbents | `data/election/2026/candidates.json` ([table](docs/CANDIDATES.md)) | CEC registration results (2026-08-31 – 09-04) as reported by CNA, LTN, UDN, CTS, Yahoo and others (links in `election.json`) |
| Parties and colours (zh/ja/en names) | `data/election/2026/parties.json` | — |
| Offices on the ballot, referendum case 22 | `data/election/2026/election.json` | CEC via press |
| Map: counties, townships, villages | `data/map/villages-10t.json` | [taiwan-atlas](https://github.com/dkaoster/taiwan-atlas) (MIT), derived from MOI boundary data |
| Candidate portraits (33 of 81) | `assets/photos/<id>.jpg` | Mostly official Legislative Yuan and council portraits; see `assets/photos/CREDITS.json` |
| Results | `data/election/2026/results.json` | Pre-election placeholder (no votes yet) |

Caveats:

- These are the **registered** candidates. The CEC finishes its eligibility review
  on 10/16 and draws ballot numbers (號次) on **10/23**, so `ballot_no` is `null`
  until then. Once numbers are drawn, update the JSON and the app sorts cards by them.
- Some English romanisations are best-effort (`romanization_guess: true`).
- Portraits were found for 33 candidates. The others get an automatic
  party-coloured avatar showing their surname. Run `tools/fetch_photos.py` on a
  machine with internet access to look for free-licensed Wikimedia Commons
  portraits for the rest.
- 5 portraits come from Wikimedia Commons without recorded author information.
  Look up their attribution before publishing (see `CREDITS.json`).
- Only the 22 mayor/magistrate races are modelled in detail. The other 11,029 seats on
  the same ballot (councillors, township chiefs, village chiefs, …) appear as counts.

## Build & run

Prerequisites (Ubuntu/Debian):

```sh
sudo apt install build-essential libvulkan1 mesa-vulkan-drivers libglfw3-dev \
                 glslang-tools fonts-noto-cjk git
# Bazel: install bazelisk; .bazelversion pins Bazel 8.3.1
```

macOS should also work with `brew install glfw glslang molten-vk` plus a CJK
font directory (`--font_dir`), but only Linux has been tested.

```sh
bazel run //:twn_election                          # window, Traditional Chinese
bazel run //:twn_election -- --lang=ja             # 日本語
bazel run //:twn_election -- --lang=en             # English
bazel run //:twn_election -- --simulate            # demo counting night (synthetic!)
bazel run //:twn_election -- --focus=63000         # start in Taipei City
bazel run //:twn_election -- --results=/path/live.json   # watch a live feed file

# Headless (no display/GPU; works with lavapipe):
bazel run //:twn_election -- --headless --screenshot=$PWD/out.png --focus=67000 --lang=en
bazel test //tests/...
```

The first build fetches Skia (pinned commit) and compiles about 600 Skia sources,
which takes a few minutes. Later builds are incremental.

`--help` lists all flags. Region codes are the MOI codes in the atlas: county
`63000`, township `63000030`, village `63000030001`, and so on.

### Controls

| Input | Action |
|---|---|
| Left-drag | Pan (the grabbed point stays under the cursor) |
| Right/middle-drag | Orbit / tilt |
| Wheel, `+`/`-` | Zoom to cursor |
| Click | Drill down into the region |
| Esc / Backspace / right-click | Up one level |
| Arrows/WASD, Q/E, R/F | Pan, rotate, tilt |
| `1` `2` `3` `4` | Colour by leading party / counting progress / turnout / referendum |
| `L` | Cycle language 繁體中文 → 日本語 → English |
| `Space`, `[` `]` | Pause the simulation, rewind/skip 10% |
| `Home` | Reset the view · `H` help overlay |

## Results feed

The app polls the results file once a second and reloads it when its modification
time changes. Writers should replace the file atomically: write to a temp file,
then rename it over the original. Format (`twn_election.results/v1`):

```json
{
  "schema": "twn_election.results/v1",
  "status": "counting",
  "source": "CEC live feed", "updated_at": "2026-11-28T17:05:00+08:00",
  "races": {
    "63000-mayor": { "regions": {
      "63000030":    { "votes": {"63000-02": 12345, "63000-05": 23456},
                       "eligible": 250000, "ballots_cast": 160000,
                       "units_counted": 120, "units_total": 180 },
      "63000010001": { "votes": {"63000-02": 321, "63000-05": 456}, "units_counted": 1, "units_total": 1 }
    } }
  },
  "referendums": { "ref-22": { "regions": { "TW": { "agree": 0, "disagree": 0, "eligible": 0 } } } }
}
```

Region codes can be `TW`, county, township or village codes. Candidate IDs are listed
in [docs/CANDIDATES.md](docs/CANDIDATES.md). Useful helpers:

```sh
bazel run //tools:results_tool -- --template > feed.json       # zeroed skeleton for every race
bazel run //tools:results_tool -- --simulate=0.5 > sim.json    # synthetic village-level file
bazel run //tools:results_tool -- --check=feed.json            # validate + print county totals
```

## Architecture

```
MODULE.bazel            bzlmod deps: rules_cc, vulkan_headers, glm, nlohmann_json, earcut, stb,
                        freetype, googletest (BCR); Skia via git_repository + our BUILD overlay;
                        module extensions for host GLFW and the GLSL compiler
bazel/                  shader_tools.bzl (finds glslangValidator/glslc), shaders.bzl
                        (GLSL -> SPIR-V -> embedded C++), system_libs.bzl (host GLFW)
third_party/skia/       skia.BUILD overlay + skia_srcs.bzl (generated by tools/gen_skia_srcs.py
                        from Skia's gn/*.gni lists): CPU raster, SkSL, FreeType, directory font mgr
src/geo/                TopoJSON decoder, projection (km, with insets), region hierarchy,
                        picking, label points, earcut-based extruded mesh builder
src/election/           data model, results parsing/aggregation, file watcher, simulator
src/render/             dlopen Vulkan loader, device/swapchain/offscreen context, renderer
                        (ground, map prisms + outlines, instanced bars, overlay), GLSL shaders
src/ui/                 Skia dashboard, i18n (zh-TW/ja/en), fonts, candidate avatars
src/app/                orbit camera, application (navigation, animation, input), main
tests/                  googletest: geo, election, camera, i18n, Skia raster
tools/                  results_tool, fetch_photos.py, gen_skia_srcs.py, screenshots.sh
```

- **Rendering.** Map meshes are built once per level and carry a per-vertex region
  "slot". Heights, colours and highlights live in a per-frame storage buffer, so
  the map animates without rebuilding geometry.
- **Dashboard.** Skia draws the dashboard into a CPU buffer, which is uploaded as a
  texture and composited over the scene with premultiplied alpha.
- **Vulkan loading.** Vulkan is loaded at runtime (`dlopen`), so the build only needs
  the Vulkan headers. The renderer uses a Vulkan 1.1 / 1.0-style render pass.
- **Skia.** Skia is not in the BCR. `MODULE.bazel` fetches it with `git_repository`
  at a pinned commit and builds it with `third_party/skia/skia.BUILD`. To update it,
  bump the commit and run
  `tools/gen_skia_srcs.py /path/to/skia > third_party/skia/skia_srcs.bzl`.
- **Local overrides.** Machine-specific settings, such as registry mirrors or
  `--override_module`, go in an untracked `user.bazelrc`.

## Licence

Code: Apache License 2.0 (see `LICENSE` and `NOTICE`). Map data: taiwan-atlas (MIT), derived from the MOI's
open government data. Candidate portraits are official government portraits or
Wikimedia Commons images; their provenance is in `assets/photos/CREDITS.json`.
Election data is compiled from public CEC announcements and press reports.

---

### 繁體中文簡介

以 C++ / Bazel (bzlmod) / Vulkan / Skia 製作的 2026 年 11 月 28 日九合一選舉互動式 3D 地圖。

- **地圖操作**：可平移、縮放、旋轉，並可從全國逐層點入縣市、鄉鎮市區與村里。
- **儀表板**：列出 22 席縣市長的 81 位登記參選人（含照片、政黨、現任與任期屆滿標示），以及同日舉行的第 22 案全國性公民投票。
- **開票資料**：開票時讀取 results JSON 即時更新；`--simulate` 為模擬資料，並非真實結果或預測。
- **語言**：介面預設為繁體中文，可按 `L` 或以 `--lang=ja` / `--lang=en` 切換為日本語或英文。

### 日本語の概要

2026年11月28日の台湾統一地方選挙（九合一選挙）を可視化するインタラクティブ3D地図です（C++ / Bazel / Vulkan / Skia）。

- **地図操作**：パン、ズーム、回転ができます。全国から県市、郷鎮市区、村里へと順に掘り下げられます。
- **ダッシュボード**：首長22ポストの候補者81人（写真・政党・現職表示）と、同日実施の住民投票第22案を表示します。
- **開票データ**：開票結果は JSON ファイルから自動で再読込されます。`--simulate` は合成データで、実際の結果や予測ではありません。
- **言語**：`L` キーまたは `--lang=ja` で日本語表示に切り替えられます（既定は繁体字中国語）。
