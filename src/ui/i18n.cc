#include "src/ui/i18n.h"

#include <array>
#include <cctype>
#include <unordered_map>

namespace twn::ui {
namespace {

using Entry = std::array<const char*, 3>;  // zh-TW, ja, en

const std::unordered_map<std::string_view, Entry>& Table() {
  static const auto* table = new std::unordered_map<std::string_view, Entry>{
      {"breaking", {"快訊", "速報", "BREAKING"}},
      {"feed.title", {"開票快訊", "開票速報", "Election night feed"}},
      {"callout.counted", {"開票 {0}", "開票率 {0}", "{0} counted"}},
      {"lead.text", {"{0} 領先 {1} {2} 票", "{0}が{1}を{2}票リード", "{0} leads {1} by {2}"}},
      {"flip.text", {"{0} 反超 {1}", "{0}が{1}を逆転", "{0} overtakes {1}"}},
      {"stamp.elected", {"當選", "当選", "WON"}},
      {"chip.declared", {"宣布勝選", "勝利宣言", "Declared"}},
      {"chip.conceded", {"承認敗選", "敗北宣言", "Conceded"}},
      {"timeline.paused", {"暫停", "一時停止", "Paused"}},
      {"ev.after_declaration", {"（曾宣布勝選）", "（勝利宣言後）", " (after declaring victory)"}},
      {"ev.first.title", {"開出首票", "開票開始", "First returns"}},
      {"ev.first.text", {"{0}：開出首批選票，{1} 暫時領先", "{0}：開票開始、{1}が先行", "{0}: first returns, {1} ahead"}},
      {"ev.lead_change.title", {"逆轉", "逆転", "Lead change"}},
      {"ev.lead_change.text", {"{0}：{1} 反超 {2}", "{0}：{1}が{2}を逆転", "{0}: {1} overtakes {2}"}},
      {"ev.victory.title", {"宣布勝選", "勝利宣言", "Victory declared"}},
      {"ev.victory.text", {"{0}：{1} 宣布勝選", "{0}：{1}が勝利宣言", "{0}: {1} declares victory"}},
      {"ev.concede.title", {"承認敗選", "敗北宣言", "Concession"}},
      {"ev.concede.text", {"{0}：{1} 承認敗選", "{0}：{1}が敗北を認める", "{0}: {1} concedes"}},
      {"ev.called.title", {"當選確定", "当選確実", "Projected winner"}},
      {"ev.called.text", {"{0}：{1} 當選確定", "{0}：{1}が当選確実", "{0}: {1} projected to win"}},
      {"ev.incumbent_trailing.title", {"現任落後", "現職劣勢", "Incumbent trailing"}},
      {"ev.incumbent_trailing.text", {"{0}：現任 {2} 落後 {1}", "{0}：現職の{2}が{1}を追う展開", "{0}: incumbent {2} trails {1}"}},
      {"ev.close.title", {"差距膠著", "大接戦", "Too close"}},
      {"ev.close.text", {"{0}：{1} 與 {2} 僅差 {3} 票", "{0}：{1}と{2}の差わずか{3}票", "{0}: {1} and {2} just {3} votes apart"}},
      {"ev.final.title", {"開票完畢", "開票終了", "Count complete"}},
      {"ev.final.text", {"{0}：開票完畢，{1} 勝出 {3} 票", "{0}：開票終了、{1}が{3}票差で勝利", "{0}: count complete, {1} wins by {3}"}},
      {"pin.pinned", {"已釘選為首頁", "ホームに固定", "Pinned as home"}},
      {"pin.unpinned", {"已取消釘選", "固定を解除", "Home unpinned"}},
      {"news.title", {"新聞輿情", "ニュース評判", "News sentiment"}},
      {"news.totals", {"利多 {0} · 利空 {1} · 中性 {2}", "好材料 {0} · 悪材料 {1} · 中立 {2}", "Good {0} · bad {1} · neutral {2}"}},
      {"news.disabled", {"未啟用新聞：以 --news 啟動（OpenAI 相容 API）", "ニュース無効：--news で起動", "News off: start with --news"}},
      {"news.good", {"新聞 · 利多", "ニュース · 好材料", "News · good"}},
      {"news.bad", {"新聞 · 利空", "ニュース · 悪材料", "News · bad"}},
      {"news.neutral", {"新聞 · 中性", "ニュース · 中立", "News · neutral"}},
      {"app.title", {"2026 九合一選舉", "2026 台湾統一地方選挙", "2026 Taiwan Local Elections"}},
      {"window.title",
       {"臺灣 2026 九合一選舉", "台湾 2026 統一地方選挙", "Taiwan 2026 Local Elections"}},
      {"status.simulation",
       {"模擬資料 SIMULATION", "模擬データ SIMULATION", "SIMULATION · synthetic data"}},
      {"status.voting", {"投票進行中 {0}–{1}", "投票中 {0}–{1}", "Polls open {0}–{1}"}},
      {"status.awaiting", {"等待開票資料", "開票データ待ち", "Awaiting results"}},
      {"status.countdown", {"距投票日 {0} 天", "投票日まで {0} 日", "{0} days to election day"}},
      {"status.counting", {"開票中", "開票中", "Counting"}},
      {"status.final", {"開票完成", "開票終了", "Final"}},
      {"header.subtitle",
       {"{0}（六）投票 {1}–{2} · 22 縣市長", "{0}（土）投票 {1}–{2} · 首長 22 ポスト",
        "Vote {0} (Sat) {1}–{2} · 22 mayors & magistrates"}},
      {"offices.title",
       {"同日改選 · 九類地方公職", "同日選挙 · 9 種類の地方公職", "On the ballot · 9 local offices"}},
      {"offices.seats", {"席次", "定数", "Seats"}},
      {"offices.registered", {"登記", "登録", "Filed"}},
      {"offices.total",
       {"合計 {0} 席 · {1} 人登記", "計 {0} 議席 · {1} 人が登録", "{0} seats · {1} candidates filed"}},
      {"area.title", {"本區概況", "地域の概要", "Area overview"}},
      {"area.size", {"面積 {0} km²", "面積 {0} km²", "Area {0} km²"}},
      {"area.towns", {"{0} 個鄉鎮市區", "{0} 郷鎮市区", "{0} townships/districts"}},
      {"area.villages", {"{0} 個村里", "{0} 村里", "{0} villages/boroughs"}},
      {"area.incumbent", {"現任", "現職", "Incumbent"}},
      {"area.term_limited", {"任期屆滿", "任期満了", "Term-limited"}},
      {"ref.title",
       {"全國性公民投票 第{0}案", "全国住民投票 第{0}案", "National referendum · Case {0}"}},
      {"ref.agree", {"同意", "賛成", "Agree"}},
      {"ref.disagree", {"不同意", "反対", "Disagree"}},
      {"ref.threshold_short",
       {"開票 {0} · 門檻：同意票≥投票權人 1/4", "開票 {0} · 成立要件：賛成≥有権者の1/4",
        "{0} counted · needs agree ≥ 1/4 of electorate"}},
      {"ref.threshold",
       {"門檻：同意多於不同意，且同意票≥投票權人總數 1/4",
        "成立要件：賛成が反対を上回り、かつ有権者総数の1/4以上",
        "Passes if agree > disagree and agree ≥ 1/4 of electorate"}},
      {"nation.title", {"縣市長 · {0} 席", "県市長 · {0} ポスト", "Mayors & magistrates · {0}"}},
      {"nation.candidates", {"{0} 位登記參選", "候補者 {0} 人", "{0} candidates"}},
      {"nation.seats_final", {"當選席次（依政黨）", "当選（政党別）", "Won, by party"}},
      {"nation.seats_leading", {"目前領先（依政黨）", "リード中（政党別）", "Leading, by party"}},
      {"nation.seats_incumbent", {"現任執政（依政黨）", "現職（政党別）", "Incumbents, by party"}},
      {"nation.count", {"{0} 人", "{0} 人", "{0}"}},
      {"race.none", {"此區無縣市長選舉資料", "この地域の首長選データなし", "No mayoral race data"}},
      {"race.title", {"{0}選舉", "{0}選挙", "{0} election"}},
      {"race.municipal", {"直轄市", "直轄市", "Special municipality"}},
      {"race.scope_city", {"全市開票結果", "全市の開票結果", "Citywide results"}},
      {"race.scope_county", {"全縣開票結果", "全県の開票結果", "Countywide results"}},
      {"race.scope_region", {"{0} 開票結果", "{0} の開票結果", "Results in {0}"}},
      {"race.progress",
       {"開票 {0} / {1} 單位（{2}）", "開票 {0} / {1} 単位（{2}）", "Counted {0} / {1} units ({2})"}},
      {"race.not_started", {"尚未開票", "未開票", "Not yet counted"}},
      {"race.turnout", {"投票率 {0}", "投票率 {0}", "Turnout {0}"}},
      {"race.incumbent", {"現任", "現職", "Incumbent"}},
      {"race.elected", {"當選", "当選", "Elected"}},
      {"race.leading", {"領先", "リード", "Leading"}},
      {"race.backed", {"{0} 支持", "{0} 支援", "backed by {0}"}},
      {"race.totals",
       {"總有效票 {0} · 選舉人 {1}", "有効票 {0} · 有権者 {1}", "Valid votes {0} · electorate {1}"}},
      {"race.order_note",
       {"候選人依登記順序排列；號次 10/23 抽籤後更新", "候補者は届出順。番号は10/23の抽選後に更新",
        "Listed in filing order; ballot numbers drawn 10/23"}},
      {"level.county", {"縣市", "県・市", "County/city"}},
      {"level.town", {"鄉鎮市區", "郷鎮市区", "Township/district"}},
      {"level.village", {"村里", "村・里", "Village/borough"}},
      {"tip.no_votes_enter", {"尚無開票數據 · 點擊進入", "開票データなし · クリックで詳細", "No votes yet · click to open"}},
      {"tip.no_votes", {"尚無開票數據", "開票データなし", "No votes yet"}},
      {"tip.enter", {"點擊進入", "クリックで詳細", "Click to open"}},
      {"inset", {"示意位置", "位置は模式的", "Not to position"}},
      {"footer.controls",
       {"左鍵拖曳 平移 · 右鍵拖曳 旋轉/傾斜 · 滾輪 縮放 · 點擊 進入下一層 · Esc 返回 · 1–4 著色：{0} · "
        "N 新聞 · P 釘選 · L 語言 · H 說明",
        "左ドラッグ 移動 · 右ドラッグ 回転/傾き · ホイール ズーム · クリック 詳細 · Esc 戻る · 1–4 "
        "配色：{0} · N ニュース · P 固定 · L 言語 · H ヘルプ",
        "Left-drag pan · right-drag orbit/tilt · wheel zoom · click drill down · Esc up · 1–4 "
        "colour: {0} · N news · P pin · L language · H help"}},
      {"mode.leader", {"領先政黨", "リード政党", "Leading party"}},
      {"mode.progress", {"開票進度", "開票率", "Counting progress"}},
      {"mode.turnout", {"投票率", "投票率", "Turnout"}},
      {"mode.referendum", {"公投（第22案）", "住民投票（第22案）", "Referendum (case 22)"}},
      {"help.title", {"操作說明", "操作方法", "Controls"}},
      {"help.1", {"左鍵拖曳 — 平移地圖", "左ドラッグ — 地図を移動", "Left-drag — pan the map"}},
      {"help.2",
       {"右鍵 / 中鍵拖曳 — 旋轉與傾斜", "右 / 中ドラッグ — 回転・傾き", "Right/middle-drag — orbit & tilt"}},
      {"help.3", {"滾輪、+ / - — 縮放", "ホイール、+ / - — ズーム", "Wheel, + / - — zoom to cursor"}},
      {"help.4",
       {"左鍵點擊 — 進入縣市 → 鄉鎮市區 → 村里", "クリック — 県市 → 郷鎮市区 → 村里へ",
        "Click — drill into county → township → village"}},
      {"help.5",
       {"Esc / Backspace / 右鍵點擊 — 返回上一層", "Esc / Backspace / 右クリック — 一つ上へ",
        "Esc / Backspace / right-click — go up a level"}},
      {"help.6",
       {"方向鍵 / WASD — 平移 · Q / E — 旋轉 · R / F — 傾斜",
        "矢印 / WASD — 移動 · Q / E — 回転 · R / F — 傾き",
        "Arrows / WASD — pan · Q / E — rotate · R / F — tilt"}},
      {"help.7",
       {"1 領先政黨 · 2 開票進度 · 3 投票率 · 4 公投", "1 リード政党 · 2 開票率 · 3 投票率 · 4 住民投票",
        "1 leading party · 2 progress · 3 turnout · 4 referendum"}},
      {"help.8",
       {"Space 暫停 · , / . 模擬速度 ×½ / ×2 · [ / ] 倒轉/快轉 30 分 · P 釘選首頁",
        "Space 一時停止 · , / . 速度 ×½ / ×2 · [ / ] 30分戻す/進める · P ホーム固定",
        "Space pause · , / . speed ×½ / ×2 · [ / ] ∓30 min · P pin as home"}},
      {"help.9",
       {"L — 切換語言（繁中 / 日本語 / English）· Home — 回首頁區域 · N — 新聞 · H — 說明",
        "L — 言語切替（繁中 / 日本語 / English）· Home — ホームへ · N — ニュース · H — 閉じる",
        "L — language (繁中 / 日本語 / English) · Home — home region · N — news · H — close"}},
  };
  return *table;
}

// Traditional -> Japanese shinjitai for characters common in Taiwanese place
// and personal names. Characters identical in both scripts are omitted.
const std::unordered_map<std::string_view, std::string_view>& KanjiMap() {
  static const auto* map = new std::unordered_map<std::string_view, std::string_view>{
      {"臺", "台"}, {"縣", "県"}, {"區", "区"}, {"鄉", "郷"}, {"灣", "湾"}, {"峽", "峡"},
      {"雙", "双"}, {"龜", "亀"}, {"萬", "万"}, {"豐", "豊"}, {"與", "与"}, {"廣", "広"},
      {"澤", "沢"}, {"濱", "浜"}, {"關", "関"}, {"鐵", "鉄"}, {"將", "将"}, {"營", "営"},
      {"學", "学"}, {"舊", "旧"}, {"壽", "寿"}, {"榮", "栄"}, {"圓", "円"}, {"國", "国"},
      {"黨", "党"}, {"會", "会"}, {"實", "実"}, {"體", "体"}, {"發", "発"}, {"權", "権"},
      {"舉", "挙"}, {"聲", "声"}, {"蘆", "芦"}, {"鹽", "塩"}, {"燈", "灯"}, {"蔣", "蒋"},
      {"賴", "頼"}, {"黃", "黄"}, {"龍", "竜"}, {"寶", "宝"}, {"惠", "恵"}, {"戰", "戦"},
      {"樂", "楽"}, {"歸", "帰"}, {"雞", "鶏"}, {"彎", "弯"}, {"畫", "画"}, {"點", "点"},
      {"邊", "辺"}, {"號", "号"}, {"貓", "猫"}, {"鶯", "鴬"}, {"檢", "検"}, {"險", "険"},
      {"驛", "駅"}, {"佛", "仏"}, {"廳", "庁"}, {"莊", "荘"}, {"條", "条"}, {"亞", "亜"},
      {"惡", "悪"}, {"兒", "児"}, {"晉", "晋"}, {"曉", "暁"}, {"櫻", "桜"}, {"氣", "気"},
      {"淺", "浅"}, {"溫", "温"}, {"滿", "満"}, {"燒", "焼"}, {"爭", "争"}, {"稱", "称"},
      {"經", "経"}, {"綠", "緑"}, {"縱", "縦"}, {"總", "総"}, {"繩", "縄"}, {"聽", "聴"},
      {"處", "処"}, {"觀", "観"}, {"讓", "譲"}, {"變", "変"}, {"轉", "転"}, {"辭", "辞"},
      {"醫", "医"}, {"釋", "釈"}, {"錢", "銭"}, {"隨", "随"}, {"雜", "雑"}, {"靜", "静"},
      {"顏", "顔"}, {"驗", "験"}, {"髮", "髪"}, {"麥", "麦"}, {"齋", "斎"}, {"齒", "歯"},
      {"瀨", "瀬"}, {"澀", "渋"}, {"拔", "抜"}, {"擇", "択"}, {"數", "数"}, {"斷", "断"},
      {"來", "来"}, {"價", "価"}, {"儉", "倹"}, {"兩", "両"}, {"劍", "剣"}, {"勞", "労"},
      {"卻", "却"}, {"單", "単"}, {"嚴", "厳"}, {"圍", "囲"}, {"壯", "壮"}, {"奧", "奥"},
      {"姬", "姫"}, {"屆", "届"}, {"巖", "巌"}, {"惱", "悩"}, {"戲", "戯"}, {"拜", "拝"},
      {"樓", "楼"}, {"濟", "済"}, {"瀧", "滝"}, {"爐", "炉"}, {"狀", "状"}, {"獻", "献"},
      {"祕", "秘"}, {"禮", "礼"}, {"竊", "窃"}, {"粹", "粋"}, {"絲", "糸"}, {"續", "続"},
      {"肅", "粛"}, {"藝", "芸"}, {"蠶", "蚕"}, {"裝", "装"}, {"覺", "覚"}, {"證", "証"},
      {"讀", "読"}, {"豬", "猪"}, {"貳", "弐"}, {"輕", "軽"}, {"遲", "遅"}, {"鄰", "隣"},
      {"銳", "鋭"}, {"錄", "録"}, {"鑄", "鋳"}, {"齊", "斉"}, {"黑", "黒"}, {"默", "黙"},
  };
  return *map;
}

size_t Utf8Len(unsigned char c) {
  return c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
}

}  // namespace

bool ParseLang(std::string_view s, Lang* out) {
  std::string l;
  for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (l == "zh" || l == "zh-tw" || l == "zh_tw" || l == "tw" || l == "zh-hant") {
    *out = Lang::kZhTW;
  } else if (l == "ja" || l == "jp" || l == "ja-jp" || l == "ja_jp") {
    *out = Lang::kJa;
  } else if (l == "en" || l == "en-us" || l == "en_us" || l == "en-gb") {
    *out = Lang::kEn;
  } else {
    return false;
  }
  return true;
}

const char* LangCode(Lang l) {
  switch (l) {
    case Lang::kJa: return "ja";
    case Lang::kEn: return "en";
    default: return "zh-TW";
  }
}

const char* LangNativeName(Lang l) {
  switch (l) {
    case Lang::kJa: return "日本語";
    case Lang::kEn: return "English";
    default: return "繁體中文";
  }
}

Lang NextLang(Lang l) {
  return static_cast<Lang>((static_cast<int>(l) + 1) % static_cast<int>(Lang::kCount));
}

const char* Tr(Lang lang, std::string_view key) {
  const auto& t = Table();
  auto it = t.find(key);
  if (it == t.end()) return key.data();
  const char* s = it->second[static_cast<int>(lang)];
  return s && *s ? s : it->second[0];
}

std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args) {
  std::string out;
  out.reserve(pattern.size() + 16);
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
        std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
      const size_t idx = pattern[i + 1] - '0';
      if (idx < args.size()) out += *(args.begin() + idx);
      i += 2;
    } else {
      out += pattern[i];
    }
  }
  return out;
}

std::string ToJapaneseKanji(std::string_view zh) {
  const auto& map = KanjiMap();
  std::string out;
  out.reserve(zh.size());
  for (size_t i = 0; i < zh.size();) {
    const size_t n = std::min(Utf8Len(static_cast<unsigned char>(zh[i])), zh.size() - i);
    const std::string_view ch = zh.substr(i, n);
    auto it = map.find(ch);
    out += it == map.end() ? ch : it->second;
    i += n;
  }
  return out;
}

std::string Localizer::RegionName(const geo::Region& r) const {
  switch (lang_) {
    case Lang::kJa: return r.level == geo::Level::kNation ? "台湾" : ToJapaneseKanji(r.name_zh);
    case Lang::kEn: return r.name_en.empty() ? r.name_zh : r.name_en;
    default: return r.name_zh;
  }
}

std::string Localizer::CandidateName(const election::Candidate& c) const {
  switch (lang_) {
    case Lang::kJa: return ToJapaneseKanji(c.name_zh);
    case Lang::kEn: return c.name_en.empty() ? c.name_zh : c.name_en;
    default: return c.name_zh;
  }
}

std::string Localizer::CandidateAltName(const election::Candidate& c) const {
  return lang_ == Lang::kEn ? c.name_zh : c.name_en;
}

std::string Localizer::PartyShort(const election::Party& p) const {
  switch (lang_) {
    case Lang::kJa: return p.short_ja.empty() ? p.short_zh : p.short_ja;
    case Lang::kEn: return p.short_en.empty() ? p.code : p.short_en;
    default: return p.short_zh;
  }
}

std::string Localizer::PartyName(const election::Party& p) const {
  switch (lang_) {
    case Lang::kJa: return p.name_ja.empty() ? p.name_zh : p.name_ja;
    case Lang::kEn: return p.name_en;
    default: return p.name_zh;
  }
}

std::string Localizer::RaceTitle(const election::Race& r) const {
  switch (lang_) {
    case Lang::kJa: return ToJapaneseKanji(r.county_zh) + "長";
    case Lang::kEn:
      return r.county_en + (r.office_zh == "縣長" ? " Magistrate" : " Mayor");
    default: return r.TitleZh();
  }
}

std::string Localizer::OfficeName(const election::OfficeSummary& o) const {
  switch (lang_) {
    case Lang::kJa: return o.name_ja;
    case Lang::kEn: return o.name_en;
    default: return o.name_zh;
  }
}

std::string Localizer::Question(const election::Referendum& r) const {
  switch (lang_) {
    case Lang::kJa: return r.question_ja.empty() ? r.question_zh : r.question_ja;
    case Lang::kEn: return r.question_en.empty() ? r.question_zh : r.question_en;
    default: return r.question_zh;
  }
}

std::string Localizer::ListSeparator() const { return lang_ == Lang::kEn ? ", " : "、"; }

}  // namespace twn::ui
