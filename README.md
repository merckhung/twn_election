# twn_election — 臺灣 2026 九合一選舉 3D 地圖

[繁體中文](README.md) · [English](README.en.md) · [日本語](README.ja.md)

以 C++20、Bazel（僅使用 bzlmod）、Vulkan 與 Skia 製作的互動式臺灣 3D 地圖及即時選舉儀表板，呈現 2026 年 11 月 28 日九合一地方選舉（民國 115 年地方公職人員選舉）。介面支援繁體中文（預設）、日本語與 English。

![全國地圖](docs/screenshots/01_nation_preelection.png)

| | |
|---|---|
| ![模擬開票](docs/screenshots/03_nation_simulation.png) | ![臺南](docs/screenshots/04_tainan_simulation.png) |
| ![日文村里畫面](docs/screenshots/05_village_ja.png) | ![英文苗栗畫面](docs/screenshots/06_miaoli_en.png) |

### 選舉之夜（模擬）

| | |
|---|---|
| ![全國開票夜](docs/screenshots/09_night_nation.png) | ![預測與宣布勝選](docs/screenshots/10_night_projections.png) |
| ![膠著選情與新聞面板](docs/screenshots/11_close_race_news.png) | ![日文介面](docs/screenshots/12_night_ja.png) |

### 圖表

| | |
|---|---|
| ![得票趨勢](docs/screenshots/13_chart_trend.png) | ![席次半圓圖](docs/screenshots/14_chart_seats.png) |
| ![差距排行](docs/screenshots/15_chart_margins.png) | ![選區總覽](docs/screenshots/17_chart_grid.png) |

[docs/demo/](docs/demo/) 收錄以無視窗模式輸出的 1280×720 示範影片：

| 影片 | 內容 |
|---|---|
| [election_night_demo.mp4](docs/demo/election_night_demo.mp4) | 以下影片串接，約 82 秒 |
| [01_whole_night_x1024.mp4](docs/demo/01_whole_night_x1024.mp4) | 16:00–23:00 全程，以 ×1024 播放 |
| [02_close_race_news_x128.mp4](docs/demo/02_close_race_news_x128.mp4) | 桃園膠著選情與模擬 LLM 分類的假新聞 |
| [03_japanese_ui.mp4](docs/demo/03_japanese_ui.mp4) | 日文介面 |
| [04_charts_and_pip_tour.mp4](docs/demo/04_charts_and_pip_tour.mp4) | 地圖、五種圖表與子母畫面導覽 |

可用 `--record=DIR --record_seconds=S --fps=F` 重新錄製；圖表導覽另加 `--chart_tour=4`，再以 ffmpeg 合成影片。

> 彩色開票畫面使用內建的 `--simulate` 模擬器。票數為隨機合成資料，不是真實結果或預測；介面會標示「模擬資料 SIMULATION」。選舉日前，程式顯示倒數、現任者與登記候選人等實際選前資訊。

## 功能

- **臺灣 3D 地圖：**以 Vulkan 繪製縣市、鄉鎮市區與村里區塊，含 4× MSAA、霧效與海面格線；金門、馬祖以獨立插圖呈現。
- **地圖操作：**拖曳平移、滾輪縮放、旋轉與傾斜，並可動畫飛往指定區域。點擊可依序從臺灣全境進入 22 個縣市、368 個鄉鎮市區及約 7,700 個村里；按 Esc、Backspace 或右鍵返回。點擊相鄰區域可直接切換。
- **Skia 儀表板：**顯示選舉倒數、開票狀態、各黨席次、22 場縣市長選舉的 81 位候選人、票數與開票進度。候選人卡包含照片、政黨、支持者與現任／任期屆滿標籤。另提供同張選票的 9 種公職及全國性公民投票第 22 案（核能）資訊。
- **政黨地圖著色：**開票前依現任者政黨著色；開票中顯示領先候選人的政黨，並以亮度表現差距。3D 長條顯示各區前三名；也可按開票進度、投票率或公投同意／不同意著色。
- **即時資料：**監看 results JSON 檔案並在檔案變動時重新載入。資料可提供不同層級；缺少的層級會由村里向上彙整至鄉鎮市區與縣市。
- **選舉之夜模擬：**16:00 投票結束後，約 16,000 個模擬投開票所分批回報，約 23:00 完成。包含領先翻轉、宣布勝選、承認敗選、預測當選及最終票數。模擬速度可從 ×1 調至 ×4096。
- **即時動畫：**票數遞增、區域脈衝與光束、卡片重新排序、地圖顏色交叉淡化、逆轉漣漪、快訊橫幅、地圖標註、當選印章及開票時間軸。
- **新聞情緒：**匯入 RSS／Atom 與 JSONL 新聞，以 OpenAI 相容模型分類，或在離線時使用關鍵字判斷；按候選人統計正負面新聞並存入 SQLite。提供模擬新聞與假 LLM 伺服器，方便試用整個流程。
- **首頁地區：**按 `P` 將目前地區設為下次啟動的首頁。
- **無視窗模式：**可在無顯示器或 GPU 的環境離線輸出 PNG；Mesa lavapipe 可供 CI 使用。

## 資料

| 內容 | 位置 | 來源 |
|---|---|---|
| 81 位縣市長候選人、政黨與現任者 | `data/election/2026/candidates.json`（[候選人表](docs/CANDIDATES.md)） | 中選會登記資料（2026-08-31 至 09-04），整理自中央社、自由時報、聯合報、華視、Yahoo 等報導，連結見 `election.json` |
| 政黨與中／日／英文名稱、色彩 | `data/election/2026/parties.json` | — |
| 選舉職務與公投第 22 案 | `data/election/2026/election.json` | 中選會公開資料與新聞報導 |
| 縣市、鄉鎮市區與村里地圖 | `data/map/villages-10t.json` | [taiwan-atlas](https://github.com/dkaoster/taiwan-atlas)（MIT），依內政部界線資料整理 |
| 候選人照片（81 位中 33 位） | `assets/photos/<id>.jpg` | 多數為立法院與議會官方照片；來源列於 `assets/photos/CREDITS.json` |
| 開票資料 | `data/election/2026/results.json` | 選前空白資料，尚無票數 |

注意事項：

- 目前列出已登記候選人。中選會預計 10/16 完成資格審查、10/23 抽出號次；號次確認前 `ballot_no` 為 `null`。完成後可更新 JSON，程式會依號次排列候選人。
- 部分英文羅馬拼音為暫譯，並以 `romanization_guess: true` 標示。
- 尚未找到照片的候選人會以政黨色彩與姓氏產生頭像。可在可連網的電腦執行 `tools/fetch_photos.py`，搜尋具自由授權的 Wikimedia Commons 照片。
- 5 張 Wikimedia Commons 照片尚未記錄作者資訊；公開發布前請查明並補上署名，詳見 `CREDITS.json`。
- 詳細資料僅涵蓋 22 場縣市長選舉；同張選票其他 11,029 個職缺（議員、鄉鎮市長、村里長等）以席次統計呈現。

## 建置與執行

Ubuntu／Debian 需求：

```sh
sudo apt install build-essential libvulkan1 mesa-vulkan-drivers libglfw3-dev \
                 libcurl4-openssl-dev libasound2-dev glslang-tools fonts-noto-cjk git
# 安裝 Bazelisk；.bazelversion 指定 Bazel 8.3.1
```

macOS 可安裝 `glfw`、`glslang`、`molten-vk`，並以 `--font_dir` 指向 CJK 字型目錄；目前僅在 Linux 測試。

```sh
bazel run //:twn_election                          # 視窗，預設繁體中文
bazel run //:twn_election -- --lang=ja             # 日本語
bazel run //:twn_election -- --lang=en             # English
bazel run //:twn_election -- --simulate            # 模擬開票（合成資料）
bazel run //:twn_election -- --simulate --sim_speed=256 --sim_clock=18:30
bazel run //:twn_election -- --simulate --news     # 模擬新聞與情緒分類
bazel run //:twn_election -- --focus=63000         # 啟動時聚焦臺北市
bazel run //:twn_election -- --results=/path/live.json

# 無視窗輸出（可使用 lavapipe）
bazel run //:twn_election -- --headless --screenshot=$PWD/out.png --focus=67000 --lang=en
bazel test //tests/...
```

首次建置會下載固定版本的 Skia，並編譯約 600 個 Skia 原始檔，需數分鐘；後續建置為增量編譯。`--help` 可列出所有參數。地區代碼使用內政部圖資代碼，例如縣市 `63000`、鄉鎮市區 `63000030`、村里 `63000030001`。

### 操作快捷鍵

| 按鍵 | 功能 |
|---|---|
| 左鍵拖曳 | 平移地圖 |
| 右鍵／中鍵拖曳 | 旋轉／傾斜 |
| 滾輪、`+`／`-` | 以游標位置縮放 |
| 點擊地圖 | 進入下一層區域 |
| Esc／Backspace／右鍵點擊 | 返回上一層 |
| 方向鍵／WASD、Q／E、R／F | 平移、旋轉、傾斜 |
| `1` `2` `3` `4` | 依領先政黨、開票進度、投票率、公投結果著色 |
| `L` | 切換繁體中文、日本語與 English |
| `Space` | 暫停／繼續模擬 |
| `,`／Page Down；`.`／Page Up | 模擬速度減半／加倍 |
| `[` `]` | 倒退／快轉 30 分鐘 |
| `F7` | 從 16:00 重新開始模擬 |
| `F2`–`F6` | 開啟得票趨勢、席次、差距、政黨得票或選區總覽圖表；再按一次返回地圖 |
| `G`／`M` | 切換圖表／返回地圖 |
| `I` | 顯示或隱藏最新消息子母畫面 |
| `←` `→` | 在趨勢圖切換選區 |
| `N` | 新聞情緒面板 |
| `V` | 開關背景音樂 |
| `P` | 固定或取消目前首頁地區 |
| `Home` | 前往固定地區或臺灣全境；`H` 顯示說明 |

## 選舉之夜模擬

`--simulate` 會播放一場完整的模擬開票夜：投票於 16:00 結束，16:15 左右開始回報，主要票數在 17:00 至 20:00 間進入，最後批次約於 23:00 完成。約 16,000 個合成投開票所依村里分批回報。部分選區的早期與晚期回報略有不同，因此會出現領先翻轉。候選人實力隨機產生，不依政黨設定；模擬數字不是預測。

速度範圍是 ×1（七小時實際時間）至 ×4096，預設 ×64，完整模擬約需六分半。按快捷鍵 `.`／Page Up 加速，`,`／Page Down 減速。按 `Space` 暫停。`F7` 會從 16:00 重新開始並清除目前模擬票數、事件和圖表歷史。

程式預設播放 144 BPM 的背景音樂，包含鼓組、切分低音與多層旋律。按 `V` 開關音樂，或以 `--no_music` 停用。

```sh
bazel run //tools:results_tool -- --night  # 列印完整事件時間軸
```

### 事件與快訊

| 事件 | 說明 | 快訊 |
|---|---|---|
| 領先翻轉 | 新領先者至少領先 0.1% 票數（至少 30 票），避免頻繁跳動 | 是 |
| 宣布勝選 | 團隊自行宣布，通常早於開票完成及中選會正式公告 | 是 |
| 承認敗選 | 落後者稍後承認敗選 | 是 |
| 預測當選 | 領先差距高於剩餘預估票數；至少開出 25% | 是 |
| 現任者落後 | 開票至少 30% 後，現任者落後 | 是 |
| 開出首票 | 該選區第一批票數進入 | 否 |
| 選情膠著 | 開票至少 70%，領先差距低於 1% | 否 |
| 開票完成 | 所有單位完成計票；中選會仍會在數日後公告當選 | 否 |

### 動畫

| 更新 | 動畫 |
|---|---|
| 新票數 | 數字遞增、候選人卡顯示新增票數、區域高亮與光束、票數流入圖增加 |
| 地方領先翻轉 | 新政黨色彩漣漪、地圖顏色淡入淡出、顯示超前提示 |
| 穩定開票 | 約每 2.5 秒在開票最多的區域顯示領先票數與進度 |
| 選區領先翻轉 | BREAKING 橫幅、候選人照片、卡片重排與金色光暈 |
| 宣布勝選／承認敗選 | 橫幅、地圖標註與候選人卡狀態標籤 |
| 預測當選 | 候選人卡蓋上「當選」印章，地圖顯示金色漣漪與標記 |
| 新聞分類 | 卡片顯示正負面統計，區域顯示柔和漣漪 |
| 開票時間軸 | 顯示 16:00–23:00 播放位置、每 5 分鐘票數與事件標記 |

## 圖表與子母畫面

地圖是主要畫面，可用快捷鍵或點擊地圖下方分頁開啟五種圖表。按 `M`、Esc 或地圖分頁回到地圖；`G` 循環切換畫面。圖表開啟時，背景地圖仍會播放動畫。

| 按鍵 | 圖表 | 內容 |
|---|---|---|
| F2 | 得票趨勢 | 各候選人 16:00–23:00 得票率、領先翻轉、預測時點及開票進度；`←`／`→` 切換選區 |
| F3 | 席次半圓 | 22 席依政黨分組，顯示預測、領先席次與過半門檻 12 席 |
| F4 | 差距排行 | 依領先者與第二名差距排序，標示開票進度及預測當選 |
| F5 | 政黨得票 | 22 場選舉的全國政黨票數、占比與領先／當選席次 |
| F6 | 選區總覽 | 22 個選區的進度、前兩名得票率及宣布／預測狀態 |

點擊差距排行、總覽或席次可返回地圖並聚焦該縣市；在趨勢圖點擊會切換選區。

**子母畫面（最新快訊）：**顯示最新快訊、迷你票數圖或候選人新聞情緒。每 6.5 秒切換內容，新快訊優先顯示。地圖時位於右下角，圖表時移至左下角。按 `I` 開關，按 × 關閉，點擊即可在地圖檢視該選區。

## 新聞情緒與 LLM

`--news` 會啟動背景工作程序，讀取 `data/news/feeds.json` 中的 RSS／Atom feed 與 `data/news/inbox/` 的 JSONL 檔，並將新聞寫入 SQLite。分類內容包含相關候選人、對各候選人的正面／負面／中性情緒與一句摘要。儀表板的候選人卡顯示正負面新聞數；按 `N` 查看分布與最新標題。

分類可使用任何 OpenAI 相容的 Chat Completions API，向 `{base_url}/chat/completions` 發送請求並取得 JSON 回覆；包括 OpenAI、Azure OpenAI、OpenRouter、Ollama、vLLM、LM Studio 與 llama.cpp server。

```sh
export OPENAI_API_KEY=sk-...                       # 或 TWN_LLM_API_KEY
bazel run //:twn_election -- --news --llm_model=gpt-4o-mini
bazel run //:twn_election -- --news --llm_base_url=http://localhost:11434/v1 --llm_model=qwen2.5
bazel run //:twn_election -- --news --no_llm       # 僅使用離線關鍵字分類
```

若沒有 API 金鑰或本機端點，或請求失敗，系統會退回關鍵字分類（`model = "heuristic"`）。選舉事件也會依固定規則生成新聞，例如領先翻轉對新領先者為正面、對被超越者為負面（`model = "event-rule"`）。

**模擬新聞：**使用 `--simulate --news` 或 `--mock_news` 產生合成競選新聞，以測試 LLM、介面與動畫。每則新聞會標上「【模擬】」、來源 `MOCK` 與 `mock://` 網址，並明示並非真實報導。內容僅包含造勢、背書、民調變化與對手批評，不會捏造真實人物的犯罪或醜聞。每篇保留預期情緒標籤，可用 `news_tool --eval=N` 評估分類準確度。`tools/mock_openai_server.py` 提供不需 API 金鑰的相容測試伺服器。

```sh
python3 tools/mock_openai_server.py --port 8089 &
bazel run //:twn_election -- --simulate --news --llm_base_url=http://127.0.0.1:8089/v1 --llm_model=mock
bazel run //tools:news_tool -- --eval=200 --llm_model=gpt-4o-mini
bazel run //tools:news_tool -- --stats
bazel run //tools:news_tool -- --fetch | --ingest=f.jsonl | --latest=10 | --mock=50 --rss=mock.xml
```

## SQLite 資料庫

資料庫預設為專案根目錄的 `twn_election.db`，可用 `--db` 更改。資料庫採 WAL 模式，讓新聞工作程序寫入時 UI 仍可讀取。

| 資料表 | 內容 |
|---|---|
| `settings` | 設定，例如首頁地區 `home_region` |
| `race_totals` | 每個時間點、各選區候選人的票數與開票單位數 |
| `events` | 選舉之夜事件（類型、選區、領先者、差距、進度） |
| `articles` | 新聞文章、摘要與分類模型；網址唯一 |
| `assessments` | 每篇新聞對各候選人的情緒（-1／0／+1）與理由 |

重新啟動模擬或重新執行程式時，會清除標示為模擬的資料列。

## 開票結果資料格式

程式每秒檢查 results 檔案，偵測到修改時間變更時重新載入。寫入端應先寫入暫存檔，再以 rename 原子替換原檔。格式為 `twn_election.results/v1`：

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
      "63000010001": { "votes": {"63000-02": 321, "63000-05": 456},
                       "units_counted": 1, "units_total": 1 }
    } }
  },
  "referendums": { "ref-22": { "regions": { "TW": { "agree": 0, "disagree": 0, "eligible": 0 } } } },
  "declarations": [ { "race": "63000-mayor", "candidate": "63000-05", "type": "victory",
                      "time": "2026-11-28T19:40:00+08:00" } ]
}
```

地區代碼可使用 `TW`、縣市、鄉鎮市區或村里代碼。候選人 ID 見[候選人表](docs/CANDIDATES.md)。可使用以下工具：

```sh
bazel run //tools:results_tool -- --template > feed.json       # 建立各選區的空白模板
bazel run //tools:results_tool -- --simulate=0.5 > sim.json    # 產生村里層級合成資料
bazel run //tools:results_tool -- --check=feed.json            # 驗證資料並列出縣市票數
```

## 架構

```text
MODULE.bazel            bzlmod 相依項目：rules_cc、Vulkan headers、glm、nlohmann_json、earcut、stb、
                        freetype、sqlite3、googletest（BCR）；Skia 以 git_repository 固定版本下載；
                        module extension 尋找系統 GLFW、libcurl 與 GLSL 編譯器
bazel/                  shader_tools.bzl、shaders.bzl、system_libs.bzl
third_party/skia/       Skia BUILD overlay 與產生的來源清單
src/geo/                TopoJSON、地圖投影、區域階層、點選與擠出網格
src/election/           選舉資料、結果解析彙整、檔案監看、模擬器、事件追蹤
src/news/               RSS／Atom／JSONL、libcurl、LLM 與關鍵字分類、模擬新聞
src/store/              SQLite 設定、結果歷史、事件、新聞與分類紀錄
src/render/             Vulkan 載入、裝置／swapchain／離屏環境、地圖與著色器
src/ui/                 Skia 儀表板、快訊、時間軸、圖表、i18n、字型與頭像
src/app/                相機、操作與動畫、音樂、地圖效果、模擬與主程式
 tests/                  地圖、選舉、模擬、新聞、相機、i18n 與 Skia 測試
 tools/                  results_tool、news_tool、mock_openai_server.py、fetch_photos.py 等
```

地圖網格建立後以頂點區域索引重用幾何；高度、色彩與高亮狀態放在每幀儲存緩衝區。Skia 在 CPU 上繪製儀表板，再以含 premultiplied alpha 的紋理合成到 Vulkan 畫面。Vulkan 以執行時載入方式使用，建置時只需標頭檔。Skia 不在 BCR；`MODULE.bazel` 會由固定 Git commit 下載，並使用 `third_party/skia/skia.BUILD` 建置。更新版本後，以 `tools/gen_skia_srcs.py /path/to/skia > third_party/skia/skia_srcs.bzl` 重新產生來源清單。機器專屬設定（例如 registry mirror 或 `--override_module`）可放在未追蹤的 `user.bazelrc`。

## 授權

程式碼採 MIT 授權，詳見 `LICENSE` 與 `NOTICE`。地圖資料來自 taiwan-atlas（MIT），並依政府開放資料整理。候選人照片為政府官方照片或 Wikimedia Commons 圖片；來源與署名資訊列於 `assets/photos/CREDITS.json`。選舉資料整理自中選會公開公告與新聞報導。
