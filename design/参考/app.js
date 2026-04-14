const BASE_WIDTH = 528;
const BASE_HEIGHT = 792;
const DEFAULT_LANGUAGE = "zh";
const DRAWER_BREAKPOINT = 1080;

const asset = (path) => `../app/asset/${path}`;
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const pad2 = (value) => String(value).padStart(2, "0");
const formatMinutes = (minutes) => {
  const whole = Math.floor(Math.max(0, minutes));
  return `${pad2(Math.floor(whole / 60))}:${pad2(whole % 60)}`;
};

const formatDurationMs = (ms) => {
  const totalSeconds = Math.max(0, Math.floor(ms / 1000));
  return `${pad2(Math.floor(totalSeconds / 60))}:${pad2(totalSeconds % 60)}`;
};

const readingItems = [
  {
    titleZh: "城南旧事",
    titleEn: "Old Days in the South of the City",
    metaZh: "TXT · 78 KB · 2026-01-14 15:03",
    metaEn: "TXT · 78 KB · 2026-01-14 15:03",
    content:
      "冬天快要过去的时候，风就变得轻了一些。窗台上的光慢慢爬进来，像一页还没翻开的书。\n\n我把脚步放轻，听见远处一点点风声。它不是催促，更像提醒：还有些东西值得慢一点看完。\n\n阅读的页面总是这样，字句往前走，心却会在某一行停一下。停住的时候，刚好可以把日子理顺一点。",
  },
  {
    titleZh: "雨巷来信",
    titleEn: "Letters From the Rain Alley",
    metaZh: "EPUB · 112 KB · 2026-01-13 18:42",
    metaEn: "EPUB · 112 KB · 2026-01-13 18:42",
    content:
      "雨把街道洗得很安静，像有人提前把嘈杂收起来了。\n\n这封信没有写完，但也许不必写完。路灯和雨水合在一起，足够把一句话照亮。\n\n等你翻到下一页，也许答案还没有来，但心会先落稳一点。",
  },
  {
    titleZh: "白昼笔记",
    titleEn: "Daylight Notes",
    metaZh: "TXT · 54 KB · 2026-01-11 09:08",
    metaEn: "TXT · 54 KB · 2026-01-11 09:08",
    content:
      "白天的记录总是更像整理。把看见的、听见的、想起的东西排好队，日子就不再四处散开。\n\n有些句子写下来并不为了发表，只是为了不让它们轻轻滑走。\n\n今天的这一页，先放一句：慢一点，也可以把事情做好。",
  },
  {
    titleZh: "海边回声",
    titleEn: "Echoes by the Sea",
    metaZh: "EPUB · 91 KB · 2026-01-09 21:26",
    metaEn: "EPUB · 91 KB · 2026-01-09 21:26",
    content:
      "海风不是一种声音，而是一串慢慢铺开的回响。\n\n有人站在岸边说话，也有人只是看着潮水，像看着一件正在归位的事。\n\n如果今天没有更多消息，那就把这一页留给海。它会替你把节奏放慢。",
  },
];

const musicItems = [
  { titleZh: "雨巷留声", titleEn: "Rain Alley Echo", metaZh: "木心朗读集 · 03:28", metaEn: "Muxin reading · 03:28" },
  { titleZh: "窗下小夜曲", titleEn: "Serenade by the Window", metaZh: "白描钢琴 · 04:06", metaEn: "Minimal piano · 04:06" },
  { titleZh: "湖边清晨", titleEn: "Morning by the Lake", metaZh: "自然采样 · 05:12", metaEn: "Field recording · 05:12" },
  { titleZh: "安静地走路", titleEn: "Walking Quietly", metaZh: "散文配乐 · 02:54", metaEn: "Essay score · 02:54" },
];

const recordItems = [
  { name: "2026-01-14 15:40 录音 01", duration: 36, meta: "TF · 录音成功 · 01/14 15:40" },
  { name: "2026-01-14 14:05 录音 02", duration: 128, meta: "TF · 录音成功 · 01/14 14:05" },
  { name: "2026-01-13 20:11 录音 03", duration: 74, meta: "TF · 录音成功 · 01/13 20:11" },
  { name: "2026-01-12 09:18 录音 04", duration: 302, meta: "TF · 录音成功 · 01/12 09:18" },
];

const homeTiles = [
  { id: "reading-list", titleZh: "阅读", titleEn: "Reading", icon: asset("home/home_reading.png"), subtitleZh: "在线阅读", subtitleEn: "Books and articles" },
  { id: "pet", titleZh: "陪伴成长", titleEn: "Companion Growth", icon: asset("home/home_pet.png"), subtitleZh: "陪伴页面", subtitleEn: "Companion view" },
  { id: "ai-dou", titleZh: "AI小豆", titleEn: "AI Dou", icon: asset("home/home_ai.png"), subtitleZh: "语音对话", subtitleEn: "Voice chat" },
  { id: "time-manage", titleZh: "时间管理", titleEn: "Time", icon: asset("home/home_clock.png"), subtitleZh: "番茄 / 时间", subtitleEn: "Pomodoro / clock" },
  { id: "weather", titleZh: "天气", titleEn: "Weather", icon: asset("home/home_weather.png"), subtitleZh: "同步天气", subtitleEn: "Weather sync" },
  { id: "calendar", titleZh: "日历", titleEn: "Calendar", icon: asset("home/home_calendar.png"), subtitleZh: "月视图", subtitleEn: "Monthly view" },
  { id: "recorder", titleZh: "录音", titleEn: "Recorder", icon: asset("home/home_record.png"), subtitleZh: "开始与播放", subtitleEn: "Record & play" },
  { id: "music-list", titleZh: "音乐", titleEn: "Music", icon: asset("home/home_music.png"), subtitleZh: "音乐列表", subtitleEn: "Music library" },
  { id: "settings", titleZh: "设置", titleEn: "Settings", icon: asset("home/home_settings.png"), subtitleZh: "系统配置", subtitleEn: "System settings" },
];

const pageGroups = [
  {
    title: "Home",
    items: ["home", "standby", "status-detail"],
  },
  {
    title: "Content",
    items: ["reading-list", "reading-detail", "pet", "ai-dou", "time-manage", "pomodoro", "datetime", "weather", "calendar"],
  },
  {
    title: "Media",
    items: ["recorder", "record-list", "music-list", "music-player"],
  },
  {
    title: "Settings",
    items: ["settings", "brightness", "language", "bluetooth-config", "wallpaper"],
  },
];

const pageMeta = {
  home: { titleZh: "主页", titleEn: "Home", back: null },
  standby: { titleZh: "待机", titleEn: "Standby", back: "home" },
  "reading-list": { titleZh: "在线阅读", titleEn: "Reading", back: "home" },
  "reading-detail": { titleZh: "阅读详情", titleEn: "Reading Detail", back: "reading-list" },
  pet: { titleZh: "陪伴成长", titleEn: "Companion Growth", back: "home" },
  "ai-dou": { titleZh: "AI小豆", titleEn: "AI Dou", back: "home" },
  "time-manage": { titleZh: "时间管理", titleEn: "Time", back: "home" },
  pomodoro: { titleZh: "番茄时间", titleEn: "Pomodoro", back: "time-manage" },
  datetime: { titleZh: "日期与时间", titleEn: "Date & Time", back: "time-manage" },
  weather: { titleZh: "天气", titleEn: "Weather", back: "home" },
  calendar: { titleZh: "日历", titleEn: "Calendar", back: "home" },
  "status-detail": { titleZh: "快捷状态", titleEn: "Quick Status", back: "home" },
  recorder: { titleZh: "录音", titleEn: "Recorder", back: "home" },
  "record-list": { titleZh: "录音记录", titleEn: "Recordings", back: "recorder" },
  "music-list": { titleZh: "音乐", titleEn: "Music", back: "home" },
  "music-player": { titleZh: "音乐播放器", titleEn: "Music Player", back: "music-list" },
  settings: { titleZh: "设置", titleEn: "Settings", back: "home" },
  brightness: { titleZh: "屏幕亮度", titleEn: "Brightness", back: "settings" },
  language: { titleZh: "语言", titleEn: "Language", back: "settings" },
  "bluetooth-config": { titleZh: "蓝牙配置", titleEn: "Bluetooth Config", back: "settings" },
  wallpaper: { titleZh: "壁纸测试", titleEn: "Wallpaper", back: "settings" },
};

const state = {
  language: DEFAULT_LANGUAGE,
  page: getInitialPage(),
  pageScroll: 0,
  statusReturn: "home",
  readingIndex: 0,
  readingSettingsOpen: false,
  readingFont: 22,
  readingSpacing: 2,
  aiMode: "ready",
  aiCopy: "",
  recorderRecording: false,
  recorderStartedAt: Date.now(),
  recorderElapsedMs: 0,
  recordSelectedIndex: 0,
  musicSelectedIndex: 0,
  musicPlaying: false,
  brightness: 72,
  volume: 56,
  clock: { hour: 15, minute: 30 },
  bluetoothEnabled: true,
  bluetoothConnected: true,
  networkEnabled: true,
  use4g: true,
  selectedNameIndex: 0,
  wallpaperIndex: 0,
  weather: {
    location: "当前位置",
    code: "99",
    temp: 23,
    summaryZh: "晴",
    summaryEn: "Sunny",
    humidity: 58,
    windDirectionZh: "东南",
    windDirectionEn: "SE",
    windScale: "3",
    feelsLike: 25,
    lastUpdate: new Date(),
  },
  calendar: createCalendarState(new Date(2026, 0, 14)),
  pomodoro: {
    running: false,
    completed: false,
    remainingMs: 25 * 60 * 1000,
    startedAt: Date.now(),
    startRemainingMs: 25 * 60 * 1000,
  },
};

const drawer = document.getElementById("drawer");
const device = document.getElementById("device");
const drawerToggle = document.getElementById("drawerToggle");
const overlay = document.getElementById("overlay");

function getInitialPage() {
  const hash = window.location.hash.replace(/^#/, "");
  return pageMeta[hash] ? hash : "home";
}

function tr(zh, en) {
  return state.language === "en" ? en : zh;
}

function currentTimeLabel() {
  const now = new Date();
  return `${pad2(now.getHours())}:${pad2(now.getMinutes())}`;
}

function currentDateLabel() {
  const now = new Date();
  const weekday = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"][now.getDay()];
  return `${now.getFullYear()}/${pad2(now.getMonth() + 1)}/${pad2(now.getDate())} ${weekday}`;
}

function createCalendarState(date) {
  return {
    year: date.getFullYear(),
    month: date.getMonth() + 1,
    selectedDay: date.getDate(),
  };
}

function monthName(year, month) {
  return `${year}-${pad2(month)}`;
}

function weatherIconPath(code) {
  const known = {
    99: "weather/w99.png",
    0: "sunny.png",
    1: "weather/w1.png",
    2: "weather/w2.png",
    3: "weather/w3.png",
    4: "weather/w4.png",
    5: "weather/w5.png",
    6: "weather/w6.png",
    7: "weather/w7.png",
  };
  return asset(known[code] || "weather/w99.png");
}

function formatDateTime(date) {
  return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())} ${pad2(date.getHours())}:${pad2(date.getMinutes())}`;
}

function screenShell({ pageId, title, back, content, noChrome = false, clickableStatus = false }) {
  const statusClass = clickableStatus ? "status-bar is-clickable" : "status-bar";
  return `
    <section class="screen screen--${pageId}">
      ${noChrome ? "" : renderStatusBar(statusClass, clickableStatus)}
      ${noChrome ? "" : renderTitleBar(pageId, title, back)}
      <div class="screen-content">${content}</div>
    </section>
  `;
}

function renderStatusBar(statusClass, clickableStatus) {
  const weatherText = tr(state.weather.summaryZh, state.weather.summaryEn);
  const windText = tr(state.weather.windDirectionZh, state.weather.windDirectionEn);
  const meta = `${currentDateLabel()}\n${weatherText} ${state.weather.temp}°C · ${windText}`;
  return `
    <header class="${statusClass}" ${clickableStatus ? `data-page="status-detail" data-status-return="${state.page}"` : ""}>
      <div class="status-left">
        <div class="status-time js-status-clock">${currentTimeLabel()}</div>
        <div class="status-meta">${meta}</div>
      </div>
      <div class="status-right" aria-hidden="true">
        <div class="status-chip">${state.bluetoothEnabled ? "BT" : "--"}</div>
        <div class="status-chip">${state.networkEnabled ? (state.use4g ? "4G" : "PAN") : "OFF"}</div>
        <div class="status-icon"></div>
        <div class="status-battery">
          <div class="status-battery-fill" style="right:${100 - clamp(state.brightness, 5, 100)}%"></div>
        </div>
      </div>
    </header>
  `;
}

function renderTitleBar(pageId, title, back) {
  if (!back) {
    return "";
  }
  return `
    <header class="title-bar">
      <button class="nav-button" type="button" data-page="${back}">${tr("返回", "Back")}</button>
      <div class="title-bar-title">${title}</div>
      <button class="nav-button is-primary" type="button" data-page="home">${tr("主页", "Home")}</button>
    </header>
  `;
}

function renderDrawer() {
  const languageButtons = `
    <button class="pill ${state.language === "zh" ? "is-active" : ""}" type="button" data-action="set-language" data-language="zh">中文</button>
    <button class="pill ${state.language === "en" ? "is-active" : ""}" type="button" data-action="set-language" data-language="en">English</button>
  `;

  const groups = pageGroups
    .map(
      (group) => `
        <section class="drawer-group">
          <h2 class="drawer-group-title">${group.title}</h2>
          ${group.items
            .map((pageId) => {
              const meta = pageMeta[pageId];
              const icon = pageId === "home"
                ? homeTiles[0].icon
                : pageId === "standby"
                  ? asset("home/home_clock.png")
                  : pageId === "status-detail"
                    ? asset("home/home_battery.png")
                    : pageId === "reading-detail"
                      ? asset("home/home_reading.png")
                      : pageId === "ai-dou"
                        ? asset("home/home_ai.png")
                        : pageId === "pomodoro"
                          ? asset("home/home_clock.png")
                          : pageId === "datetime"
                            ? asset("home/home_clock.png")
                            : pageId === "music-player"
                              ? asset("home/home_music.png")
                              : pageId === "brightness"
                                ? asset("home/home_settings.png")
                                : pageId === "language"
                                  ? asset("home/home_settings.png")
                                  : pageId === "bluetooth-config"
                                    ? asset("home/home_bluetooth.png")
                                    : pageId === "wallpaper"
                                      ? asset("home/home_settings.png")
                                      : pageId === "record-list"
                                        ? asset("home/home_record.png")
                                        : pageId === "recorder"
                                          ? asset("home/home_mic.png")
                                          : pageId === "settings"
                                            ? asset("home/home_settings.png")
                                            : pageId === "weather"
                                              ? asset("home/home_weather.png")
                                              : pageId === "calendar"
                                                ? asset("home/home_calendar.png")
                                                : pageId === "music-list"
                                                  ? asset("home/home_music.png")
                                                  : pageId === "time-manage"
                                                    ? asset("home/home_clock.png")
                                                    : pageId === "pet"
                                                      ? asset("home/home_pet.png")
                                                      : asset("home/home_reading.png");
              return `
                <button class="page-link ${state.page === pageId ? "is-active" : ""}" type="button" data-page="${pageId}">
                  <img class="page-link-icon" src="${icon}" alt="" />
                  <div>
                    <div class="page-link-title">${tr(meta.titleZh, meta.titleEn)}</div>
                    <div class="page-link-subtitle">${pageId}</div>
                  </div>
                </button>
              `;
            })
            .join("")}
        </section>
      `,
    )
    .join("");

  return `
    <div class="drawer-head">
      <div class="drawer-kicker">XiaoZhi H5</div>
      <h1 class="drawer-title">页面原型</h1>
      <p class="drawer-subtitle">把当前工程里的原生页面，按 H5 的方式整理成一套可切换的移动端原型。</p>
    </div>
    <div class="drawer-toolbar">
      ${languageButtons}
    </div>
    <div class="drawer-groups">${groups}</div>
  `;
}

function renderHome() {
  const tiles = homeTiles
    .map(
      (tile) => `
        <button class="home-tile" type="button" data-page="${tile.id}">
          <img class="home-tile-icon" src="${tile.icon}" alt="" />
          <div class="home-tile-label">${tr(tile.titleZh, tile.titleEn)}</div>
          <div class="home-tile-sub">${tr(tile.subtitleZh, tile.subtitleEn)}</div>
        </button>
      `,
    )
    .join("");

  const content = `<div class="home-grid">${tiles}</div>`;
  return screenShell({
    pageId: "home",
    title: "",
    back: null,
    content,
    clickableStatus: true,
  });
}

function renderStandby() {
  const date = new Date();
  const memo = tr("备忘：明天早上给老婆做双蛋三明治\n加红米粥", "Memo: make breakfast tomorrow\n(two-egg sandwich + rice porridge)");
  const quote = tr("末日来临前我又度过了美好的一天！", "I had a beautiful day before the end of the world.");
  const content = `
    <div class="section-stack">
      <div class="card pad" style="padding-top: 12px;">
        <div class="weather-location" style="text-align:left;">${date.getFullYear()}/${pad2(date.getMonth() + 1)}/${pad2(date.getDate())}</div>
        <div class="card-subtitle" style="margin-top: 4px;">${tr("农历待同步", "Lunar date pending sync")}</div>
        <div class="card-row" style="margin-top: 12px;">
          <div>
            <div class="card-title" style="margin-bottom: 2px;">${tr("星期四  --°C  --", "Thu  --°C  --")}</div>
            <div class="card-subtitle">${tr("室内：--°C    --%", "Indoor: --°C    --%")}</div>
          </div>
          <div class="summary-value" style="font-size: 86px;">${pad2(date.getHours())}:${pad2(date.getMinutes())}</div>
        </div>
        <div class="card-subtitle" style="margin-top: 8px;">${tr("闹钟 7:30", "Alarm 7:30")}</div>
      </div>

      <div class="card pad">
        <div class="card-title">${tr("备忘", "Memo")}</div>
        <p class="card-subtitle" style="white-space: pre-line; margin-top: 6px;">${memo}</p>
      </div>

      <div class="card pad">
        <div class="card-title">${tr("一句话", "Quote")}</div>
        <p class="card-subtitle" style="font-size: 18px; margin-top: 6px;">${quote}</p>
        <div class="card-subtitle" style="text-align:right; margin-top: 8px;">—— 卧豆</div>
      </div>
    </div>
  `;

  return `
    <section class="screen screen--standby">
      <div class="screen-content" style="padding-top: 18px;">
        ${content}
      </div>
      <div style="position:absolute; inset:0; z-index:2;" data-page="home" aria-hidden="true"></div>
    </section>
  `;
}

function renderReadingList() {
  const summary = `
    <div class="card pad">
      <div class="summary-strip">
        <div>
          <div class="summary-value">${readingItems.length}</div>
          <div class="summary-copy">${tr("共 4 本 / 4 个条目", "4 items in the library")}</div>
        </div>
        <div class="badge">${tr("可点击阅读", "Tap to open")}</div>
      </div>
    </div>
  `;

  const list = readingItems
    .map(
      (item, index) => `
        <button class="list-card ${state.readingIndex === index ? "is-active" : ""}" type="button" data-action="open-reading" data-index="${index}">
          <div class="list-card-title">${tr(item.titleZh, item.titleEn)}</div>
          <p class="list-card-meta">${tr(item.metaZh, item.metaEn)}</p>
        </button>
      `,
    )
    .join("");

  const footer = `
    <div class="button-row" style="justify-content:flex-end;">
      <button class="button" type="button">${tr("上翻", "Prev")}</button>
      <button class="button" type="button">${tr("下翻", "Next")}</button>
    </div>
  `;

  return screenShell({
    pageId: "reading-list",
    title: tr(pageMeta["reading-list"].titleZh, pageMeta["reading-list"].titleEn),
    back: pageMeta["reading-list"].back,
    content: `<div class="section-stack">${summary}<div class="list">${list}</div>${footer}</div>`,
    clickableStatus: true,
  });
}

function renderReadingDetail() {
  const item = readingItems[state.readingIndex] || readingItems[0];
  const body = item.content
    .split("\n\n")
    .map((paragraph) => `<p style="margin: 0 0 ${state.readingSpacing * 2 + 8}px; line-height: ${1.55 + state.readingSpacing * 0.04}; font-size: ${state.readingFont}px;">${paragraph}</p>`)
    .join("");

  const settingsOverlay = state.readingSettingsOpen
    ? `
      <div class="settings-overlay">
        <div class="settings-panel">
          <div class="card-title">${tr("阅读设置", "Reading Settings")}</div>
          <div class="settings-row">
            <div class="field-label">${tr("字号", "Font")}</div>
            <button class="button" type="button" data-action="reading-font" data-delta="-2">-</button>
            <div class="field-value">${state.readingFont}px</div>
            <button class="button is-filled" type="button" data-action="reading-font" data-delta="2">+</button>
          </div>
          <div class="settings-row">
            <div class="field-label">${tr("行距", "Spacing")}</div>
            <button class="button" type="button" data-action="reading-spacing" data-delta="-1">-</button>
            <div class="field-value">${state.readingSpacing}</div>
            <button class="button is-filled" type="button" data-action="reading-spacing" data-delta="1">+</button>
          </div>
          <div class="button-row" style="justify-content:flex-end; margin-top: 12px;">
            <button class="button is-accent" type="button" data-action="toggle-reading-settings">${tr("关闭", "Close")}</button>
          </div>
        </div>
      </div>
    `
    : "";

  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="summary-strip">
          <div>
            <div class="card-title" style="margin-bottom: 6px;">${tr(item.titleZh, item.titleEn)}</div>
            <div class="card-subtitle">${tr(item.metaZh, item.metaEn)}</div>
          </div>
          <div class="badge">${tr("章节 01", "Page 01")}</div>
        </div>
      </div>

      <div class="card pad" style="position: relative;">
        <div class="card-subtitle" style="font-size: 15px; line-height: 1.8;">${body}</div>
        ${settingsOverlay}
      </div>

      <div class="button-row">
        <button class="button" type="button">${tr("上页", "Prev")}</button>
        <button class="button" type="button" data-action="toggle-reading-settings">${tr("阅读设置", "Settings")}</button>
        <button class="button is-filled" type="button">${tr("下页", "Next")}</button>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "reading-detail",
    title: tr(pageMeta["reading-detail"].titleZh, pageMeta["reading-detail"].titleEn),
    back: pageMeta["reading-detail"].back,
    content,
    clickableStatus: true,
  });
}

function renderPet() {
  const content = `
    <div class="section-stack">
      <div class="card pad" style="text-align:center;">
        <div class="card-title" style="font-size: 44px; margin-bottom: 6px;">${tr("陪伴成长", "Companion Growth")}</div>
        <p class="card-subtitle" style="font-size: 18px;">
          ${tr(
            "宠物状态、互动喂养与陪伴内容可继续在这一页扩展，当前先恢复主视觉与页面跳转。",
            "Pet status, feeding, and companion content can keep growing here. This page keeps the main visual and navigation flow.",
          )}
        </p>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "pet",
    title: tr(pageMeta.pet.titleZh, pageMeta.pet.titleEn),
    back: pageMeta.pet.back,
    content,
    clickableStatus: true,
  });
}

function renderAiDou() {
  const face = state.aiMode === "listening" ? asset("sleepy2.png") : asset("funny2.png");
  const mouth = state.aiMode === "listening" ? tr("正在聆听", "Listening") : state.aiMode === "speaking" ? tr("正在回答", "Responding") : tr("静候你开口", "Ready for you");
  const copy = state.aiMode === "listening"
    ? tr("请说话...", "Please speak...")
    : state.aiMode === "speaking"
      ? tr("我正在整理刚刚听到的内容。", "I am organizing what I just heard.")
      : tr("今天想聊什么？你可以问我阅读内容、让我整理想法，或者直接说一句想记录的话。", "What would you like to talk about today? Ask about your reading, let me organize your thoughts, or speak a note directly.");

  const content = `
    <div class="section-stack">
      <div class="card pad" style="text-align:center;">
        <img src="${face}" alt="" style="width:180px; height:180px; object-fit:contain; margin: 0 auto;" />
      </div>
      <div class="card pad">
        <div class="card-title" style="text-align:center;">${mouth}</div>
        <p class="card-subtitle" style="font-size: 18px; line-height: 1.65; text-align:center; margin-top: 10px;">${copy}</p>
        <div class="badge" style="margin: 14px auto 0;">${state.networkEnabled ? tr("网络正常", "Network online") : tr("网络异常", "Network offline")}</div>
      </div>
      <button class="button is-filled" type="button" data-action="toggle-ai-talk" style="min-height: 58px; font-size: 18px;">
        ${state.aiMode === "listening" ? tr("点击停止并发送", "Tap to stop and send") : state.aiMode === "speaking" ? tr("点击重新说话", "Tap to speak again") : tr("点击开始说话", "Tap to talk")}
      </button>
    </div>
  `;

  return screenShell({
    pageId: "ai-dou",
    title: tr(pageMeta["ai-dou"].titleZh, pageMeta["ai-dou"].titleEn),
    back: pageMeta["ai-dou"].back,
    content,
    clickableStatus: true,
  });
}

function renderTimeManage() {
  const content = `
    <div class="section-stack">
      <button class="list-card" type="button" data-page="pomodoro">
        <div class="list-card-title">${tr("番茄时间", "Pomodoro")}</div>
        <p class="list-card-meta">${tr("专注计时与休息切换", "Focus timer and break cycle")}</p>
      </button>
      <button class="list-card" type="button" data-page="datetime">
        <div class="list-card-title">${tr("日期与时间", "Date & Time")}</div>
        <p class="list-card-meta">${tr("手动校准当前时间", "Adjust the device clock")}</p>
      </button>
    </div>
  `;

  return screenShell({
    pageId: "time-manage",
    title: tr(pageMeta["time-manage"].titleZh, pageMeta["time-manage"].titleEn),
    back: pageMeta["time-manage"].back,
    content,
    clickableStatus: true,
  });
}

function renderPomodoro() {
  const percent = state.pomodoro.remainingMs / (25 * 60 * 1000);
  const display = formatDurationMs(state.pomodoro.remainingMs);
  const content = `
    <div class="section-stack">
      <div class="card pad" style="text-align:center;">
        <div class="card-title">${state.pomodoro.running ? tr("状态：专注中", "Status: Focused") : state.pomodoro.completed ? tr("状态：已完成", "Status: Completed") : state.pomodoro.remainingMs < 25 * 60 * 1000 ? tr("状态：已暂停", "Status: Paused") : tr("状态：未开始", "Status: Ready")}</div>
        <div class="timer-display js-pomodoro-time" data-value="${state.pomodoro.remainingMs}">${display}</div>
        <div class="card-subtitle js-pomodoro-detail" style="margin-top: 8px;">${tr("剩余", "Remaining")} ${Math.round((1 - percent) * 100)}%</div>
        <div class="progress" style="margin-top: 16px;">
          <div class="progress-fill js-pomodoro-progress" style="width: ${Math.max(0, percent * 100)}%;"></div>
        </div>
      </div>
      <div class="button-row">
        <button class="button is-filled js-pomodoro-primary" type="button" data-action="pomodoro-toggle">${state.pomodoro.running ? tr("暂停", "Pause") : state.pomodoro.completed ? tr("开始", "Start") : state.pomodoro.remainingMs < 25 * 60 * 1000 ? tr("继续", "Resume") : tr("开始", "Start")}</button>
        <button class="button" type="button" data-action="pomodoro-reset">${tr("重置", "Reset")}</button>
      </div>
      <p class="card-subtitle js-pomodoro-hint">${state.pomodoro.running ? tr("专注时页面会每秒刷新，暂停后进度会保留。", "The page refreshes every second while running.") : tr("点击开始后即可进入番茄专注，状态会在本轮会话内保留。", "Start a focus session and keep the state during this session.")}</p>
    </div>
  `;

  return screenShell({
    pageId: "pomodoro",
    title: tr(pageMeta.pomodoro.titleZh, pageMeta.pomodoro.titleEn),
    back: pageMeta.pomodoro.back,
    content,
    clickableStatus: true,
  });
}

function renderDatetime() {
  const { year, month, day } = state.calendar;
  const current = new Date(year, month - 1, day, state.clock.hour, state.clock.minute);
  const row = (label, key, value) => `
    <div class="card row-card" style="padding: 10px 12px; display:grid; grid-template-columns: 48px 1fr 1fr 1fr; gap: 8px; align-items:center;">
      <div class="field-label">${label}</div>
      <button class="button" type="button" data-action="datetime-step" data-field="${key}" data-delta="-1">-</button>
      <div class="field-value" style="text-align:center;">${value}</div>
      <button class="button is-filled" type="button" data-action="datetime-step" data-field="${key}" data-delta="1">+</button>
    </div>
  `;

  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-title">${tr("当前设备时间", "Current Device Time")}</div>
        <div class="timer-display" style="font-size: 40px; margin-top: 8px;">${current.getFullYear()} / ${pad2(current.getMonth() + 1)} / ${pad2(current.getDate())} ${pad2(state.clock.hour)} : ${pad2(state.clock.minute)}</div>
      </div>
      ${row(tr("年", "Year"), "year", year)}
      ${row(tr("月", "Month"), "month", pad2(month))}
      ${row(tr("日", "Day"), "day", pad2(day))}
      ${row(tr("时", "Hour"), "hour", pad2(state.clock.hour))}
      ${row(tr("分", "Minute"), "minute", pad2(state.clock.minute))}
    </div>
  `;

  return screenShell({
    pageId: "datetime",
    title: tr(pageMeta.datetime.titleZh, pageMeta.datetime.titleEn),
    back: pageMeta.datetime.back,
    content,
    clickableStatus: true,
  });
}

function renderWeather() {
  const summary = tr(state.weather.summaryZh, state.weather.summaryEn);
  const wind = tr(state.weather.windDirectionZh, state.weather.windDirectionEn);
  const content = `
    <div class="weather-hero">
      <div class="weather-location">${state.weather.location || tr("当前位置", "Current Location")}</div>
      <img class="weather-icon" src="${weatherIconPath(state.weather.code)}" alt="" />
      <div class="weather-temp">${state.weather.temp}℃</div>
      <div class="weather-summary">${tr("联网后将自动同步当前天气", "Weather syncs automatically when online")}</div>
    </div>
    <div class="metrics" style="margin-top: 18px;">
      <div class="metric">
        <div class="metric-label">${tr("湿度", "Humidity")}</div>
        <div class="metric-value">${state.weather.humidity}%</div>
      </div>
      <div class="metric">
        <div class="metric-label">${tr("风向", "Wind")}</div>
        <div class="metric-value">${wind} ${state.weather.windScale}级</div>
      </div>
      <div class="metric">
        <div class="metric-label">${tr("体感", "Feels Like")}</div>
        <div class="metric-value">${state.weather.feelsLike}℃</div>
      </div>
    </div>
    <div class="card pad" style="margin-top: 14px;">
      <div class="card-title">${summary}</div>
      <p class="card-subtitle" style="margin-top: 8px;">${tr("今天", "Today")} ${summary}，${tr("出门前记得留意温差变化。", "mind the temperature gap before heading out.")}</p>
      <p class="card-subtitle js-weather-update" style="margin-top: 8px;">${tr("上次更新: ", "Last Update: ")}${formatDateTime(state.weather.lastUpdate)}</p>
      <div class="card-subtitle" style="margin-top: 8px;">${tr("可以点击底部按钮手动刷新一次。", "Tap the button below to refresh once.")}</div>
    </div>
    <button class="button is-filled" type="button" data-action="weather-refresh" style="min-height: 52px; font-size: 16px; margin-top: 14px;">${tr("立即刷新", "Refresh")}</button>
  `;

  return screenShell({
    pageId: "weather",
    title: tr(pageMeta.weather.titleZh, pageMeta.weather.titleEn),
    back: pageMeta.weather.back,
    content,
    clickableStatus: true,
  });
}

function renderCalendar() {
  const year = state.calendar.year;
  const month = state.calendar.month;
  const first = new Date(year, month - 1, 1);
  const firstWeekday = (first.getDay() + 6) % 7;
  const daysInMonth = new Date(year, month, 0).getDate();
  const prevDays = new Date(year, month - 1, 0).getDate();
  const today = new Date();
  const cells = [];

  const weekdays = ["一", "二", "三", "四", "五", "六", "日"];
  for (const day of weekdays) {
    cells.push(`<div class="calendar-weekday">${tr(`周${day}`, day)}</div>`);
  }

  for (let index = 0; index < 42; index += 1) {
    let day;
    let muted = false;
    let cellYear = year;
    let cellMonth = month;

    if (index < firstWeekday) {
      day = prevDays - firstWeekday + index + 1;
      muted = true;
      cellMonth = month - 1;
      if (cellMonth < 1) {
        cellMonth = 12;
        cellYear -= 1;
      }
    } else if (index >= firstWeekday + daysInMonth) {
      day = index - firstWeekday - daysInMonth + 1;
      muted = true;
      cellMonth = month + 1;
      if (cellMonth > 12) {
        cellMonth = 1;
        cellYear += 1;
      }
    } else {
      day = index - firstWeekday + 1;
    }

    const isToday =
      year === today.getFullYear() &&
      month === today.getMonth() + 1 &&
      day === today.getDate() &&
      !muted;
    const isSelected = !muted && day === state.calendar.selectedDay;

    cells.push(`
      <button class="calendar-day ${muted ? "is-muted" : ""} ${isToday ? "is-today" : ""} ${isSelected ? "is-selected" : ""}" type="button" data-action="calendar-select" data-year="${cellYear}" data-month="${cellMonth}" data-day="${day}">
        ${day}
      </button>
    `);
  }

  const content = `
    <div class="calendar-header">
      <button class="button" type="button" data-action="calendar-month" data-delta="-1">${tr("上月", "Prev")}</button>
      <div style="text-align:center;">
        <div class="calendar-title">${monthName(year, month)}</div>
        <div class="calendar-meta">${tr("当前月视图", "Current month view")}</div>
      </div>
      <button class="button is-filled" type="button" data-action="calendar-today">${tr("今天", "Today")}</button>
    </div>
    <div class="calendar-grid">${cells.join("")}</div>
    <div class="calendar-summary">
      <div class="card pad">
        <div class="card-title">${tr("今日摘要", "Summary")}</div>
        <p class="card-subtitle">${tr("农历待同步", "Lunar date pending sync")}</p>
      </div>
      <div class="card pad">
        <div class="card-title">${tr("日程", "Schedule")}</div>
        <p class="card-subtitle">${tr("暂无更多安排，页面可继续接入提醒与节假日信息。", "No more items yet. This page can grow into reminders and holidays later.")}</p>
      </div>
    </div>
    <button class="button" type="button" data-action="calendar-month" data-delta="1" style="margin-top: 14px; min-height: 48px;">${tr("下月", "Next")}</button>
  `;

  return screenShell({
    pageId: "calendar",
    title: tr(pageMeta.calendar.titleZh, pageMeta.calendar.titleEn),
    back: pageMeta.calendar.back,
    content,
    clickableStatus: true,
  });
}

function renderStatusDetail() {
  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-title">${tr("设备状态", "Device Status")}</div>
        <p class="card-subtitle">${tr("亮度、音量、蓝牙和网络状态可在这里快速查看与调整。", "Brightness, volume, Bluetooth, and network status live here.")}</p>
      </div>

      <div class="card pad">
        <div class="card-row">
          <div>
            <div class="field-label">${tr("亮度", "Brightness")}</div>
            <div class="field-value">${state.brightness}%</div>
          </div>
          <div class="badge">${tr(state.brightness === 0 ? "已关闭" : "已开启", state.brightness === 0 ? "Off" : "On")}</div>
        </div>
        <input class="range" type="range" min="0" max="100" value="${state.brightness}" data-action="range-brightness" />
      </div>

      <div class="card pad">
        <div class="card-row">
          <div>
            <div class="field-label">${tr("音量", "Volume")}</div>
            <div class="field-value">${state.volume}%</div>
          </div>
          <div class="badge">${tr("可调节", "Adjustable")}</div>
        </div>
        <input class="range" type="range" min="0" max="100" value="${state.volume}" data-action="range-volume" />
      </div>

      <div class="switch-grid">
        <div class="card pad">
          <div class="field-label">${tr("蓝牙", "Bluetooth")}</div>
          <p class="card-subtitle">${state.bluetoothEnabled ? tr("开启", "On") : tr("关闭", "Off")}</p>
        </div>
        <div class="card pad">
          <div class="field-label">${tr("网络", "Network")}</div>
          <p class="card-subtitle">${state.networkEnabled ? tr("在线", "Online") : tr("离线", "Offline")}</p>
        </div>
        <div class="card pad">
          <div class="field-label">${tr("连接", "Connection")}</div>
          <p class="card-subtitle">${state.use4g ? tr("4G 在线", "4G online") : tr("蓝牙共享", "Bluetooth PAN")}</p>
        </div>
      </div>
    </div>
  `;

  const returnTo = state.statusReturn || "home";
  return screenShell({
    pageId: "status-detail",
    title: tr(pageMeta["status-detail"].titleZh, pageMeta["status-detail"].titleEn),
    back: returnTo,
    content,
    clickableStatus: false,
  });
}

function renderRecorder() {
  const buttonIcon = state.recorderRecording ? asset("录音开启.png") : asset("录音未开启.png");

  const content = `
    <div class="section-stack">
      <button class="record-button ${state.recorderRecording ? "is-recording" : ""}" type="button" data-action="toggle-recording">
        <img class="record-button-icon" src="${buttonIcon}" alt="" />
        <div class="record-button-label">${state.recorderRecording ? tr("取消录音", "Cancel Recording") : tr("开始录音", "Start Recording")}</div>
      </button>

      <div class="card pad">
        <div class="summary-strip">
          <div>
            <div class="summary-value js-recorder-elapsed">${formatDurationMs(state.recorderElapsedMs)}</div>
            <div class="summary-copy">${tr("录音时长", "Elapsed time")}</div>
          </div>
          <div class="badge">${state.recorderRecording ? tr("录音中", "Recording") : tr("待命", "Idle")}</div>
        </div>
      </div>

      <button class="list-card" type="button" data-page="record-list">
        <div class="list-card-title">${tr("录音记录", "Recordings")}</div>
        <p class="list-card-meta">${tr("查看 TF 卡上的录音文件", "View recordings saved on TF card")}</p>
      </button>
    </div>
  `;

  return screenShell({
    pageId: "recorder",
    title: tr(pageMeta.recorder.titleZh, pageMeta.recorder.titleEn),
    back: pageMeta.recorder.back,
    content,
    clickableStatus: true,
  });
}

function renderRecordList() {
  const selected = recordItems[state.recordSelectedIndex] || recordItems[0];
  const list = recordItems
    .map((item, index) => {
      const active = index === state.recordSelectedIndex;
      return `
        <button class="record-item ${active ? "is-active" : ""}" type="button" data-action="select-record" data-index="${index}">
          <div>
            <div class="record-item-title">${item.name}</div>
            <div class="record-item-meta">${item.meta} · ${pad2(Math.floor(item.duration / 60))}:${pad2(item.duration % 60)}</div>
          </div>
          <div class="badge">${active ? tr("播放中", "Playing") : tr("点击播放", "Tap to play")}</div>
        </button>
      `;
    })
    .join("");

  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="summary-strip">
          <div>
            <div class="summary-value">${recordItems.length}</div>
            <div class="summary-copy">${tr("共录音条目", "Total recordings")}</div>
          </div>
          <div class="badge">${tr("播放/停止", "Play / Stop")}</div>
        </div>
      </div>
      <div class="card pad">
        <div class="card-title">${selected.name}</div>
        <p class="card-subtitle">${selected.meta}</p>
      </div>
      <div class="record-items">${list}</div>
    </div>
  `;

  return screenShell({
    pageId: "record-list",
    title: tr(pageMeta["record-list"].titleZh, pageMeta["record-list"].titleEn),
    back: pageMeta["record-list"].back,
    content,
    clickableStatus: true,
  });
}

function renderMusicList() {
  const list = musicItems
    .map(
      (item, index) => `
        <button class="list-card ${state.musicSelectedIndex === index ? "is-active" : ""}" type="button" data-action="open-music" data-index="${index}">
          <div class="list-card-title">${tr(item.titleZh, item.titleEn)}</div>
          <p class="list-card-meta">${tr(item.metaZh, item.metaEn)}</p>
        </button>
      `,
    )
    .join("");

  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-title">${tr("全部音乐", "All Tracks")}</div>
        <p class="card-subtitle">${tr("点击曲目进入播放器。", "Tap a track to open the player.")}</p>
      </div>
      <div class="list">${list}</div>
      <div class="button-row" style="justify-content:flex-end;">
        <button class="button" type="button">${tr("上翻", "Prev")}</button>
        <button class="button" type="button">${tr("下翻", "Next")}</button>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "music-list",
    title: tr(pageMeta["music-list"].titleZh, pageMeta["music-list"].titleEn),
    back: pageMeta["music-list"].back,
    content,
    clickableStatus: true,
  });
}

function renderMusicPlayer() {
  const item = musicItems[state.musicSelectedIndex] || musicItems[0];
  const progress = state.musicPlaying ? 38 : 24;
  const content = `
    <div class="section-stack">
      <div class="music-disc"></div>
      <div class="card pad" style="text-align:center;">
        <div class="card-title">${tr(item.titleZh, item.titleEn)}</div>
        <p class="card-subtitle">${tr("木心朗读集", "Muxin reading collection")}</p>
      </div>
      <div class="card pad">
        <div class="card-row">
          <div class="field-value">00:36</div>
          <div class="field-value">03:28</div>
        </div>
        <div class="slider-line" style="margin-top: 8px;">
          <div class="slider-fill" style="width:${progress}%;"></div>
        </div>
      </div>
      <div class="button-row">
        <button class="button" type="button" data-page="music-list">${tr("上一首", "Previous")}</button>
        <button class="button is-filled" type="button" data-action="music-toggle">${state.musicPlaying ? tr("暂停", "Pause") : tr("播放", "Play")}</button>
        <button class="button" type="button" data-page="music-list">${tr("下一首", "Next")}</button>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "music-player",
    title: tr(pageMeta["music-player"].titleZh, pageMeta["music-player"].titleEn),
    back: pageMeta["music-player"].back,
    content,
    clickableStatus: true,
  });
}

function renderSettings() {
  const brightnessSummary = state.brightness === 0 ? tr("当前已关闭", "Currently off") : tr(`当前亮度 ${state.brightness}%`, `Current brightness ${state.brightness}%`);
  const content = `
    <div class="section-stack">
      <button class="list-card" type="button" data-page="brightness">
        <div class="list-card-title">${tr("屏幕亮度", "Brightness")}</div>
        <p class="list-card-meta">${brightnessSummary}</p>
      </button>
      <button class="list-card" type="button" data-page="language">
        <div class="list-card-title">${tr("语言", "Language")}</div>
        <p class="list-card-meta">${tr(state.language === "zh" ? "简体中文" : "English", state.language === "zh" ? "Simplified Chinese" : "English")}</p>
      </button>
      <button class="list-card" type="button" data-page="bluetooth-config">
        <div class="list-card-title">${tr("蓝牙配置", "Bluetooth Config")}</div>
        <p class="list-card-meta">${tr("查看蓝牙开关、连接状态与设备名预设", "Bluetooth status, connection state and device name presets")}</p>
      </button>
      <button class="list-card" type="button" data-page="wallpaper">
        <div class="list-card-title">${tr("壁纸", "Wallpaper")}</div>
        <p class="list-card-meta">${tr("进入 TF 卡图片预览测试页", "Open the TF picture preview page")}</p>
      </button>
    </div>
  `;

  return screenShell({
    pageId: "settings",
    title: tr(pageMeta.settings.titleZh, pageMeta.settings.titleEn),
    back: pageMeta.settings.back,
    content,
    clickableStatus: true,
  });
}

function renderBrightness() {
  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-row">
          <div>
            <div class="field-label">${tr("当前亮度", "Current Level")}</div>
            <div class="field-value">${state.brightness === 0 ? tr("已关闭", "Off") : `${state.brightness}%`}</div>
          </div>
          <div class="summary-value" style="font-size: 32px;">${state.brightness === 0 ? tr("关闭", "Off") : `${state.brightness}%`}</div>
        </div>
        <div class="slider-line" style="margin-top: 16px;">
          <div class="slider-fill" style="width: ${state.brightness}%;"></div>
        </div>
      </div>
      <div class="button-row">
        <button class="button" type="button" data-action="brightness-step" data-delta="-10">${tr("降低亮度", "Dim Screen")}</button>
        <button class="button is-filled" type="button" data-action="brightness-step" data-delta="10">${tr("提高亮度", "Brighten")}</button>
      </div>
      <p class="card-subtitle">${tr("关闭后保持熄屏，再次提高亮度会从 50% 恢复。", "If brightness is off, raising it again resumes from 50%.")}</p>
    </div>
  `;

  return screenShell({
    pageId: "brightness",
    title: tr(pageMeta.brightness.titleZh, pageMeta.brightness.titleEn),
    back: pageMeta.brightness.back,
    content,
    clickableStatus: true,
  });
}

function renderLanguage() {
  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-title">${tr("切换语言", "Choose Language")}</div>
        <p class="card-subtitle">${tr("切换后会立即应用到所有页面。", "Changes apply to all screens immediately.")}</p>
      </div>
      <div class="switch-row">
        <button class="button ${state.language === "zh" ? "is-filled" : ""}" type="button" data-action="set-language" data-language="zh">简体中文</button>
        <button class="button ${state.language === "en" ? "is-filled" : ""}" type="button" data-action="set-language" data-language="en">English</button>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "language",
    title: tr(pageMeta.language.titleZh, pageMeta.language.titleEn),
    back: pageMeta.language.back,
    content,
    clickableStatus: true,
  });
}

function renderBluetoothConfig() {
  const presetNames = ["ink", "ink-office", "ink-home"];
  const content = `
    <div class="section-stack">
      <div class="card pad">
        <div class="card-row">
          <div>
            <div class="field-label">${tr("蓝牙", "Bluetooth")}</div>
            <div class="field-value">${state.bluetoothEnabled ? tr("开启", "On") : tr("关闭", "Off")}</div>
          </div>
          <button class="button ${state.bluetoothEnabled ? "is-filled" : ""}" type="button" data-action="toggle-bluetooth">${state.bluetoothEnabled ? tr("关闭", "Off") : tr("开启", "On")}</button>
        </div>
        <div class="card-row">
          <div>
            <div class="field-label">${tr("连接状态", "Connection")}</div>
            <div class="field-value">${state.bluetoothConnected ? tr("已连接", "Connected") : tr("未连接", "Disconnected")}</div>
          </div>
          <div class="badge">${state.bluetoothConnected ? tr("在线", "Online") : tr("离线", "Offline")}</div>
        </div>
      </div>

      <div class="card pad">
        <div class="card-row">
          <div>
            <div class="field-label">${tr("联网方式", "Link")}</div>
            <div class="field-value">${state.use4g ? tr("4G 在线", "4G online") : tr("蓝牙共享", "Bluetooth PAN")}</div>
          </div>
          <button class="button ${state.use4g ? "is-filled" : ""}" type="button" data-action="toggle-4g">${state.use4g ? tr("切回蓝牙", "Use PAN") : tr("切到 4G", "Use 4G")}</button>
        </div>
      </div>

      <div class="card pad">
        <div class="card-title">${tr("设备名预设", "Device Name Presets")}</div>
        <div class="switch-grid" style="margin-top: 12px;">
          ${presetNames
            .map((name, index) => `<button class="button ${state.selectedNameIndex === index ? "is-filled" : ""}" type="button" data-action="select-name" data-index="${index}">${name}</button>`)
            .join("")}
        </div>
        <p class="card-subtitle" style="margin-top: 12px;">${tr("当前：", "Current: ")}${presetNames[state.selectedNameIndex]}</p>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "bluetooth-config",
    title: tr(pageMeta["bluetooth-config"].titleZh, pageMeta["bluetooth-config"].titleEn),
    back: pageMeta["bluetooth-config"].back,
    content,
    clickableStatus: true,
  });
}

function renderWallpaper() {
  const variants = [
    { path: asset("sunny.png"), label: "sunny.png" },
    { path: asset("weather/w99.png"), label: "w99.png" },
    { path: asset("home/home_weather.png"), label: "home_weather.png" },
  ];
  const variant = variants[state.wallpaperIndex % variants.length];

  const content = `
    <div class="section-stack">
      <div class="wallpaper-preview">
        <img class="preview-img" src="${variant.path}" alt="" />
      </div>
      <div class="card pad wallpaper-meta">
        <div class="card-title">${tr("壁纸预览", "Wallpaper Preview")}</div>
        <div class="card-subtitle">${tr("目录：", "Folder: ")}${tr("/pic", "/pic")}</div>
        <div class="card-subtitle">${tr("文件：", "File: ")}${variant.label}</div>
        <div class="card-subtitle">${tr("结果：", "Result: ")}${tr("预览中", "Previewing")}</div>
        <div class="card-subtitle">${tr("说明：", "Note: ")}${tr("进入 TF 卡图片预览测试页", "Open the TF picture preview page")}</div>
      </div>
      <div class="button-row">
        <button class="button" type="button" data-action="wallpaper-prev">${tr("上一张", "Prev")}</button>
        <button class="button is-filled" type="button" data-action="wallpaper-next">${tr("下一张", "Next")}</button>
      </div>
    </div>
  `;

  return screenShell({
    pageId: "wallpaper",
    title: tr(pageMeta.wallpaper.titleZh, pageMeta.wallpaper.titleEn),
    back: pageMeta.wallpaper.back,
    content,
    clickableStatus: true,
  });
}

function renderCurrentPage() {
  switch (state.page) {
    case "home":
      return renderHome();
    case "standby":
      return renderStandby();
    case "reading-list":
      return renderReadingList();
    case "reading-detail":
      return renderReadingDetail();
    case "pet":
      return renderPet();
    case "ai-dou":
      return renderAiDou();
    case "time-manage":
      return renderTimeManage();
    case "pomodoro":
      return renderPomodoro();
    case "datetime":
      return renderDatetime();
    case "weather":
      return renderWeather();
    case "calendar":
      return renderCalendar();
    case "status-detail":
      return renderStatusDetail();
    case "recorder":
      return renderRecorder();
    case "record-list":
      return renderRecordList();
    case "music-list":
      return renderMusicList();
    case "music-player":
      return renderMusicPlayer();
    case "settings":
      return renderSettings();
    case "brightness":
      return renderBrightness();
    case "language":
      return renderLanguage();
    case "bluetooth-config":
      return renderBluetoothConfig();
    case "wallpaper":
      return renderWallpaper();
    default:
      return renderHome();
  }
}

function navigate(pageId, options = {}) {
  if (!pageMeta[pageId] && pageId !== "home") {
    return;
  }
  if (pageId === "status-detail") {
    state.statusReturn = options.returnTo || state.page || "home";
  }
  state.page = pageId;
  window.location.hash = `#${pageId}`;
  render();
  closeDrawerIfMobile();
}

function render() {
  drawer.innerHTML = renderDrawer();
  device.innerHTML = renderCurrentPage();
  drawerToggle.textContent = tr("页面", "Pages");
  drawerToggle.setAttribute("aria-expanded", String(drawer.classList.contains("is-open")));
  syncScale();
}

function syncClock() {
  const clock = document.querySelector(".js-status-clock");
  if (clock) {
    clock.textContent = currentTimeLabel();
  }

  if (state.page === "standby") {
    const standbyClock = device.querySelector(".screen--standby .summary-value");
    if (standbyClock) {
      const now = new Date();
      standbyClock.textContent = `${pad2(now.getHours())}:${pad2(now.getMinutes())}`;
    }
  }

  if (state.recorderRecording) {
    state.recorderElapsedMs = Date.now() - state.recorderStartedAt;
    const elapsed = device.querySelector(".js-recorder-elapsed");
    if (elapsed) {
      elapsed.textContent = formatDurationMs(state.recorderElapsedMs);
    }
  }

  if (state.pomodoro.running) {
    const elapsed = Date.now() - state.pomodoro.startedAt;
    state.pomodoro.remainingMs = Math.max(0, state.pomodoro.startRemainingMs - elapsed);
    if (state.pomodoro.remainingMs <= 0) {
      state.pomodoro.running = false;
      state.pomodoro.completed = true;
      state.pomodoro.remainingMs = 0;
    }
    const timeNode = device.querySelector(".js-pomodoro-time");
    const progressNode = device.querySelector(".js-pomodoro-progress");
    const detailNode = device.querySelector(".js-pomodoro-detail");
    const primaryNode = device.querySelector(".js-pomodoro-primary");
    const hintNode = device.querySelector(".js-pomodoro-hint");
    if (timeNode) {
      timeNode.textContent = formatDurationMs(state.pomodoro.remainingMs);
    }
    if (progressNode) {
      progressNode.style.width = `${Math.max(0, (state.pomodoro.remainingMs / (25 * 60 * 1000)) * 100)}%`;
    }
    if (detailNode) {
      detailNode.textContent = `${tr("剩余", "Remaining")} ${Math.round((1 - state.pomodoro.remainingMs / (25 * 60 * 1000)) * 100)}%`;
    }
    if (primaryNode) {
      primaryNode.textContent = tr("暂停", "Pause");
    }
    if (hintNode) {
      hintNode.textContent = tr("专注时页面会每秒刷新，暂停后进度会保留。", "The page refreshes every second while running.");
    }
  }
}

function syncScale() {
  const preview = document.querySelector(".preview-inner");
  if (!preview) {
    return;
  }
  const rect = preview.getBoundingClientRect();
  const scale = Math.min((rect.width - 24) / BASE_WIDTH, (rect.height - 24) / BASE_HEIGHT, 1);
  document.documentElement.style.setProperty("--frame-scale", String(Math.max(0.5, scale)));
}

function openDrawer() {
  drawer.classList.add("is-open");
  overlay.hidden = false;
  drawerToggle.setAttribute("aria-expanded", "true");
}

function closeDrawer() {
  drawer.classList.remove("is-open");
  overlay.hidden = true;
  drawerToggle.setAttribute("aria-expanded", "false");
}

function closeDrawerIfMobile() {
  if (window.innerWidth <= DRAWER_BREAKPOINT) {
    closeDrawer();
  }
}

function handlePageAction(action, target) {
  switch (action) {
    case "set-language":
      state.language = target.dataset.language === "en" ? "en" : "zh";
      render();
      break;
    case "open-reading":
      state.readingIndex = Number(target.dataset.index || 0);
      navigate("reading-detail");
      break;
    case "toggle-reading-settings":
      state.readingSettingsOpen = !state.readingSettingsOpen;
      render();
      break;
    case "reading-font":
      state.readingFont = clamp(state.readingFont + Number(target.dataset.delta || 0), 18, 30);
      render();
      break;
    case "reading-spacing":
      state.readingSpacing = clamp(state.readingSpacing + Number(target.dataset.delta || 0), 0, 12);
      render();
      break;
    case "toggle-ai-talk":
      if (state.aiMode === "ready") {
        state.aiMode = "listening";
      } else if (state.aiMode === "listening") {
        state.aiMode = "speaking";
      } else {
        state.aiMode = "ready";
      }
      render();
      break;
    case "pomodoro-toggle":
      if (state.pomodoro.running) {
        state.pomodoro.running = false;
        state.pomodoro.completed = state.pomodoro.remainingMs <= 0;
      } else {
        if (state.pomodoro.remainingMs <= 0 || state.pomodoro.completed) {
          state.pomodoro.remainingMs = 25 * 60 * 1000;
          state.pomodoro.completed = false;
        }
        state.pomodoro.running = true;
        state.pomodoro.startedAt = Date.now();
        state.pomodoro.startRemainingMs = state.pomodoro.remainingMs;
      }
      render();
      break;
    case "pomodoro-reset":
      state.pomodoro.running = false;
      state.pomodoro.completed = false;
      state.pomodoro.remainingMs = 25 * 60 * 1000;
      state.pomodoro.startedAt = Date.now();
      state.pomodoro.startRemainingMs = 25 * 60 * 1000;
      render();
      break;
    case "datetime-step":
      updateDateTimeField(target.dataset.field, Number(target.dataset.delta || 0));
      render();
      break;
    case "weather-refresh":
      refreshWeather();
      render();
      break;
    case "calendar-month":
      shiftCalendarMonth(Number(target.dataset.delta || 0));
      render();
      break;
    case "calendar-today":
      state.calendar = createCalendarState(new Date());
      render();
      break;
    case "calendar-select":
      state.calendar.year = Number(target.dataset.year);
      state.calendar.month = Number(target.dataset.month);
      state.calendar.selectedDay = Number(target.dataset.day);
      render();
      break;
    case "toggle-recording":
      if (state.recorderRecording) {
        state.recorderRecording = false;
      } else {
        state.recorderRecording = true;
        state.recorderStartedAt = Date.now();
        state.recorderElapsedMs = 0;
      }
      render();
      break;
    case "select-record":
      state.recordSelectedIndex = Number(target.dataset.index || 0);
      render();
      break;
    case "open-music":
      state.musicSelectedIndex = Number(target.dataset.index || 0);
      navigate("music-player");
      break;
    case "music-toggle":
      state.musicPlaying = !state.musicPlaying;
      render();
      break;
    case "brightness-step":
      state.brightness = clamp(state.brightness + Number(target.dataset.delta || 0), 0, 100);
      render();
      break;
    case "toggle-bluetooth":
      state.bluetoothEnabled = !state.bluetoothEnabled;
      state.bluetoothConnected = state.bluetoothEnabled;
      render();
      break;
    case "toggle-4g":
      state.use4g = !state.use4g;
      render();
      break;
    case "select-name":
      state.selectedNameIndex = Number(target.dataset.index || 0);
      render();
      break;
    case "wallpaper-prev":
      state.wallpaperIndex = (state.wallpaperIndex + 2) % 3;
      render();
      break;
    case "wallpaper-next":
      state.wallpaperIndex = (state.wallpaperIndex + 1) % 3;
      render();
      break;
    default:
      break;
  }
}

function updateDateTimeField(field, delta) {
  const date = new Date(state.calendar.year, state.calendar.month - 1, state.calendar.selectedDay, 15, 30);
  switch (field) {
    case "year":
      date.setFullYear(date.getFullYear() + delta);
      break;
    case "month":
      date.setMonth(date.getMonth() + delta);
      break;
    case "day":
      date.setDate(date.getDate() + delta);
      break;
    case "hour":
      date.setHours(date.getHours() + delta);
      break;
    case "minute":
      date.setMinutes(date.getMinutes() + delta);
      break;
    default:
      return;
  }
  state.calendar.year = date.getFullYear();
  state.calendar.month = date.getMonth() + 1;
  state.calendar.selectedDay = date.getDate();
  state.clock.hour = date.getHours();
  state.clock.minute = date.getMinutes();
}

function shiftCalendarMonth(delta) {
  const date = new Date(state.calendar.year, state.calendar.month - 1, 1);
  date.setMonth(date.getMonth() + delta);
  state.calendar.year = date.getFullYear();
  state.calendar.month = date.getMonth() + 1;
  const maxDay = new Date(state.calendar.year, state.calendar.month, 0).getDate();
  state.calendar.selectedDay = Math.min(state.calendar.selectedDay, maxDay);
}

function refreshWeather() {
  const catalog = [
    { code: "0", temp: 25, summaryZh: "晴", summaryEn: "Sunny", humidity: 48, windDirectionZh: "南风", windDirectionEn: "S", windScale: "3", feelsLike: 26 },
    { code: "1", temp: 22, summaryZh: "多云", summaryEn: "Cloudy", humidity: 58, windDirectionZh: "东南风", windDirectionEn: "SE", windScale: "2", feelsLike: 23 },
    { code: "3", temp: 18, summaryZh: "小雨", summaryEn: "Light Rain", humidity: 78, windDirectionZh: "北风", windDirectionEn: "N", windScale: "4", feelsLike: 17 },
  ];
  const next = catalog[Math.floor(Math.random() * catalog.length)];
  Object.assign(state.weather, next, { lastUpdate: new Date() });
}

function handleClick(event) {
  const pageButton = event.target.closest("[data-page]");
  if (pageButton) {
    const pageId = pageButton.dataset.page;
    if (pageId === "status-detail") {
      navigate(pageId, { returnTo: pageButton.dataset.statusReturn || state.page });
      return;
    }
    navigate(pageId);
    return;
  }

  const actionTarget = event.target.closest("[data-action]");
  if (actionTarget) {
    handlePageAction(actionTarget.dataset.action, actionTarget);
    return;
  }

  if (event.target === overlay) {
    closeDrawer();
  }
}

function handleKeydown(event) {
  if (event.key === "Escape") {
    closeDrawer();
  }
}

function updateHash() {
  const hash = window.location.hash.replace(/^#/, "");
  if (pageMeta[hash] || hash === "home") {
    state.page = hash;
    render();
  }
}

function init() {
  drawer.addEventListener("click", handleClick);
  device.addEventListener("click", handleClick);
  document.addEventListener("keydown", handleKeydown);
  window.addEventListener("resize", syncScale);
  window.addEventListener("hashchange", updateHash);
  drawerToggle.addEventListener("click", () => {
    if (drawer.classList.contains("is-open")) {
      closeDrawer();
    } else {
      openDrawer();
    }
  });
  overlay.addEventListener("click", closeDrawer);

  render();
  syncClock();
  setInterval(syncClock, 1000);
}

init();
