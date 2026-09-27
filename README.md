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

### Election night (simulation)

| | |
|---|---|
| ![Night, nation view](docs/screenshots/09_night_nation.png) | ![Projections and concessions](docs/screenshots/10_night_projections.png) |
| ![Close race with news panel](docs/screenshots/11_close_race_news.png) | ![Japanese UI](docs/screenshots/12_night_ja.png) |

Demo video (56 s, headless render): [docs/demo/election_night_demo.mp4](docs/demo/election_night_demo.mp4).
It shows the whole 16:00–23:00 night at ×1024, then a close race at ×128 with mock news
classified through the mock LLM server, then the Japanese UI.

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
- **Election night.** Polls close at 16:00, then about 16,000 simulated polling stations
  report in random batches until about 23:00. Races have real lead changes, victory
  declarations, concessions, projected winners and final counts. Speed runs from ×1
  (real time) to ×4096. See [Election night](#election-night).
- **Animations for every kind of update.**
  - Numbers roll up, "+N" floaters appear, and regions pulse with beams of light.
  - Cards slide into their new order, map colours cross-fade, and ripples spread from lead changes.
  - Breaking-news banners and map callouts show who leads whom and by how much.
  - A red 當選 stamp slams onto projected winners.
  - A timeline shows vote inflow and event markers.
- **News sentiment.**
  - The app ingests RSS/Atom feeds and JSONL drops.
  - Each article is classified with any **OpenAI-compatible LLM**, or with an offline
    heuristic when no LLM is configured.
  - The app counts good and bad news per candidate and stores everything in **SQLite**.
  - Labelled mock news and a mock LLM server let you test the whole pipeline.
- **Pinned home region.** Press `P` and the app starts at that region next time.
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
                 libcurl4-openssl-dev glslang-tools fonts-noto-cjk git
# Bazel: install bazelisk; .bazelversion pins Bazel 8.3.1
```

macOS should also work with `brew install glfw glslang molten-vk` plus a CJK
font directory (`--font_dir`), but only Linux has been tested.

```sh
bazel run //:twn_election                          # window, Traditional Chinese
bazel run //:twn_election -- --lang=ja             # 日本語
bazel run //:twn_election -- --lang=en             # English
bazel run //:twn_election -- --simulate            # counting night 16:00-23:00 (synthetic!)
bazel run //:twn_election -- --simulate --sim_speed=256 --sim_clock=18:30
bazel run //:twn_election -- --simulate --news     # + mock news, LLM/heuristic sentiment
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
| `Space` | Pause / resume the simulation |
| `,` `.` | Simulation speed ×½ / ×2 (×1 real time … ×4096) |
| `[` `]` | Rewind / skip 30 simulated minutes |
| `N` | News sentiment panel |
| `P` | Pin the current region as the start ("home") region; `P` again unpins |
| `Home` | Go to the pinned region (or Taiwan) · `H` help overlay |

## Election night

`--simulate` plays a complete counting night:
- Polls close at **16:00**. The first stations report around 16:15.
- The bulk of stations report between 17:00 and 20:00, and the last by about **23:00**.
- About 16,000 synthetic stations report village by village with random batches of votes.
- In some races the early-reporting stations lean towards a different candidate than
  the late ones, so leads genuinely change hands.
- Candidate strengths are random and party-blind. **These are not forecasts.**

The simulated clock runs at ×1 (real time: 7 hours) up to ×4096. The default is ×64,
about 6½ minutes for the whole evening.

`bazel run //tools:results_tool -- --night` prints the complete event timeline.

### Events and what counts as breaking news

| Event | 繁中 | Breaking? | Rule |
|---|---|---|---|
| Lead change | 逆轉 | ✅ | A new race leader, ahead by at least 0.1% of votes (min. 30) to avoid flip-flopping |
| Victory declared | 宣布勝選 | ✅ | A campaign claims victory. This usually comes before the count is complete and always before the CEC's official announcement. Feeds send it as `declarations[]`; the simulator declares once the lead exceeds 30% of the votes still out (≥40% counted) |
| Concession | 承認敗選 | ✅ | The runner-up concedes, some minutes after the winner declares |
| Projected winner | 當選確定 | ✅ | The margin exceeds every vote still to count, estimated from votes per counted unit +15% (≥25% counted) |
| Incumbent trailing | 現任落後 | ✅ | A sitting mayor or magistrate is behind with ≥30% counted |
| First returns | 開出首票 | — | First votes in a race |
| Too close | 差距膠著 | — | Margin under 1% with ≥70% counted |
| Count complete | 開票完畢 | — | Every unit counted (the CEC certifies winners days later) |

### Animations

| Data | Animation |
|---|---|
| New votes (every batch) | Counters roll to the new totals; "+N" floaters rise from the cards; the region's prism bumps and glows, and a beam of light shoots up; the inflow histogram grows |
| Local lead flip (township/village) | Ripples in the new leader's colour; the map colour cross-fades; a callout "A 反超 B" |
| Steady inflow | Every ~2.5 s, a callout on the busiest region: "A 領先 B 3,214 票 · 開票 42%" |
| Lead change (race) | Red **BREAKING** banner with both portraits; callout; cards slide into the new order with a gold glow; triple ripple; the race-list row flashes |
| Victory declared / concession | Banner and callout; purple "宣布勝選" chip on the card; the conceding candidate's card dims with a "承認敗選" chip |
| Projected winner | Banner; a red **當選** seal slams onto the card; gold ripples and a persistent beacon over the county |
| New classified article | Callout with ▲ (good) / ▼ (bad) in green or red; the card's news counts flash; a soft ripple on the county |
| Timeline | 16:00–23:00 track with a playhead, speed indicator, a votes-per-5-minutes histogram, and coloured markers for every event |

## News sentiment (LLM)

`--news` starts a background worker:
1. It pulls the feeds in `data/news/feeds.json` (RSS/Atom, http(s) or `file://`) and any
   `*.jsonl` files in `data/news/inbox/`.
2. It stores new articles in SQLite.
3. It classifies each article: which candidates it is about, and whether it is **good, bad
   or neutral** for each of them, plus a one-line summary.

The dashboard counts good and bad news per candidate: ▲/▼ on the cards, and a full
panel on `N` with totals, a diverging bar per candidate and the latest headlines.

Classification uses any **OpenAI-compatible Chat Completions** endpoint:
`POST {base_url}/chat/completions` with a JSON-only answer. That includes OpenAI,
Azure OpenAI, OpenRouter, Ollama, vLLM, LM Studio and llama.cpp server.

```sh
export OPENAI_API_KEY=sk-...                       # or TWN_LLM_API_KEY
bazel run //:twn_election -- --news --llm_model=gpt-4o-mini
bazel run //:twn_election -- --news --llm_base_url=http://localhost:11434/v1 --llm_model=qwen2.5
bazel run //:twn_election -- --news --no_llm       # offline keyword heuristic only
```

Without an API key or local endpoint, and whenever a call fails, the worker falls back
to the keyword heuristic (`model = "heuristic"`). Election-night events also become news
items, with fixed rules: a lead change is good for the new leader and bad for the
overtaken, and so on (`model = "event-rule"`).

**Mock news.** For testing the LLM, the UI and the animations,
`--simulate --news` (or `--mock_news`) generates synthetic campaign news.
- Every item is labelled **【模擬】** (source `MOCK`, `mock://` URLs) and states that it is not a real report.
- The templates are deliberately mild: rallies, endorsements, poll moves, criticism by
  rivals. They contain no invented crimes or scandals about real people.
- Each item carries its intended sentiment, so `news_tool --eval=N` can measure a
  classifier's accuracy.
- `tools/mock_openai_server.py` is a small OpenAI-compatible server for exercising the
  full LLM path without an API key.

```sh
python3 tools/mock_openai_server.py --port 8089 &
bazel run //:twn_election -- --simulate --news --llm_base_url=http://127.0.0.1:8089/v1 --llm_model=mock
bazel run //tools:news_tool -- --eval=200 --llm_model=gpt-4o-mini   # accuracy on mock items
bazel run //tools:news_tool -- --stats                             # good/bad counts per candidate
bazel run //tools:news_tool -- --fetch | --ingest=f.jsonl | --latest=10 | --mock=50 --rss=mock.xml
```

## Database (SQLite)

`twn_election.db` sits in the project root (change it with `--db`) and uses WAL mode, so
the news worker writes while the UI reads.

| Table | Contents |
|---|---|
| `settings` | Key/value, e.g. `home_region` (the pinned start region) |
| `race_totals` | Per race and candidate: votes and units counted, one row set per results timestamp |
| `events` | Every election-night event (type, race, leader, other, margin, progress) |
| `articles` | News articles (URL-unique), their digest and the model that classified them |
| `assessments` | Per article and candidate: sentiment (-1/0/+1) and reason |

Simulated rows are flagged and cleared when a new simulation starts.

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
  "referendums": { "ref-22": { "regions": { "TW": { "agree": 0, "disagree": 0, "eligible": 0 } } } },
  "declarations": [ { "race": "63000-mayor", "candidate": "63000-05", "type": "victory",
                      "time": "2026-11-28T19:40:00+08:00" } ]
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
                        freetype, sqlite3, googletest (BCR); Skia via git_repository + our BUILD overlay;
                        module extensions for host GLFW, libcurl and the GLSL compiler
bazel/                  shader_tools.bzl (finds glslangValidator/glslc), shaders.bzl
                        (GLSL -> SPIR-V -> embedded C++), system_libs.bzl (host GLFW)
third_party/skia/       skia.BUILD overlay + skia_srcs.bzl (generated by tools/gen_skia_srcs.py
                        from Skia's gn/*.gni lists): CPU raster, SkSL, FreeType, directory font mgr
src/geo/                TopoJSON decoder, projection (km, with insets), region hierarchy,
                        picking, label points, earcut-based extruded mesh builder
src/election/           data model, results parsing/aggregation, file watcher, election-night
                        simulator (stations, declarations), event tracker (breaking news)
src/news/               RSS/Atom + JSONL ingestion, libcurl HTTP, OpenAI-compatible LLM and
                        heuristic classifiers, mock news generator, background worker
src/store/              SQLite store (settings, results history, events, news, assessments)
src/render/             dlopen Vulkan loader, device/swapchain/offscreen context, renderer
                        (ground, map prisms + outlines, instanced bars, overlay), GLSL shaders
src/ui/                 Skia dashboard + live layer (banners, call-outs, feed, timeline,
                        rolling numbers, stamps), i18n (zh-TW/ja/en), fonts, avatars
src/app/                orbit camera, application (navigation, animation, input, map effects,
                        pinned home), election-night glue (live.cc), main
tests/                  googletest: geo, election, night (simulation/events), news/store,
                        camera, i18n, Skia raster
tools/                  results_tool, news_tool, mock_openai_server.py, fetch_photos.py,
                        gen_skia_srcs.py, screenshots.sh
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

Code: MIT (see `LICENSE` and `NOTICE`). Map data: taiwan-atlas (MIT), derived from the MOI's
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
