#pragma once

// ---------------------------------------------------------------------------
// Спільний шаблон I18N/lang-switch повторюється в кожній сторінці окремо
// (кожен PROGMEM-рядок незалежний, спільного JS-файлу поки немає - свідомий
// компроміс заради простоти на цьому етапі).
// ---------------------------------------------------------------------------

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>UPS Monitor</title>
<style>
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:20px; }
  h1 { font-size: 1.3rem; display: flex; justify-content: space-between; align-items: center; }
  table { width:100%; border-collapse: collapse; margin-top: 12px; }
  th, td { padding: 10px; text-align: left; border-bottom: 1px solid #333; }
  th { color: #888; font-weight: normal; font-size: 0.85rem; }
  .val { font-size: 1.1rem; font-variant-numeric: tabular-nums; }
  .neg { color: #ff6b6b; }
  .pos { color: #6bcb77; }
  .level { white-space: nowrap; }
  .bar { display: inline-flex; gap: 2px; vertical-align: middle; margin-right: 8px; }
  .seg { width: 10px; height: 14px; border-radius: 2px; background: #333; }
  .bar-pct { font-variant-numeric: tabular-nums; font-size: 0.85rem; color: #ccc; }
  footer { margin-top: 20px; font-size: 0.8rem; color: #666; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; }
  a { color: #7aa2f7; text-decoration: none; }
  a:hover { text-decoration: underline; }
  .btn-restart { background: #333; color: #ff6b6b; border: 1px solid #555; padding: 4px 8px; border-radius: 4px; cursor: pointer; font-size: 0.75rem; }
  .btn-restart:hover { background: #442222; border-color: #ff6b6b; }
  .lang-switch { font-size: 0.75rem; color: #777; }
  .lang-switch a { color: #777; padding: 2px 4px; }
  .lang-switch a.active { color: #7aa2f7; font-weight: bold; }
</style>
</head>
<body>
<h1>
  <span>UPS Monitor</span>
  <span style="display:flex; align-items:center; gap:12px;">
    <span class="lang-switch">
      <a href="#" id="lang-uk" onclick="setLang('uk');return false;">UA</a>/<a href="#" id="lang-en" onclick="setLang('en');return false;">EN</a>
    </span>
    <span style="font-size: 0.8rem; color: #777;" id="fw">v-</span>
  </span>
</h1>
<table id="t">
  <tr><th data-i18n="th_channel">Канал</th><th data-i18n="th_level">Рівень</th><th data-i18n="th_voltage">Напруга</th><th data-i18n="th_current">Струм</th><th data-i18n="th_power">Потужність</th></tr>
</table>
<footer>
  <div><span data-i18n="updated_label">Оновлено:</span> <span id="ts">-</span> &nbsp;|&nbsp; <a href="/settings" data-i18n="settings_link">Налаштування</a> &nbsp;|&nbsp; <a href="/update" data-i18n="ota_link">OTA оновлення</a></div>
  <button class="btn-restart" onclick="rebootDevice()" data-i18n="reboot_btn">Перезавантажити</button>
</footer>
<script>
const I18N = {
  uk: {
    th_channel: "Канал", th_level: "Рівень", th_voltage: "Напруга", th_current: "Струм", th_power: "Потужність",
    updated_label: "Оновлено:", settings_link: "Налаштування", ota_link: "OTA оновлення",
    reboot_btn: "Перезавантажити",
    reboot_confirm: "Ви дійсно хочете перезавантажити пристрій?",
    reboot_alert: "Пристрій перезавантажується. Сторінка оновиться за кілька секунд...",
    reboot_sent: "Запит відправлено",
    unit_v: "В", unit_a: "А", unit_w: "Вт"
  },
  en: {
    th_channel: "Channel", th_level: "Level", th_voltage: "Voltage", th_current: "Current", th_power: "Power",
    updated_label: "Updated:", settings_link: "Settings", ota_link: "OTA update",
    reboot_btn: "Reboot",
    reboot_confirm: "Are you sure you want to reboot the device?",
    reboot_alert: "Device is rebooting. The page will refresh in a few seconds...",
    reboot_sent: "Request sent",
    unit_v: "V", unit_a: "A", unit_w: "W"
  }
};
let lang = localStorage.getItem('lang') || (navigator.language && navigator.language.startsWith('en') ? 'en' : 'uk');

function t(key) { return I18N[lang][key] || key; }

function applyLang() {
  document.documentElement.lang = lang;
  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = t(el.getAttribute('data-i18n')); });
  document.getElementById('lang-uk').classList.toggle('active', lang === 'uk');
  document.getElementById('lang-en').classList.toggle('active', lang === 'en');
}

function setLang(l) {
  lang = l;
  localStorage.setItem('lang', l);
  applyLang();
  poll();
}

function levelColor(percent, isLoad) {
  // Заряд: червоний <20%, жовтий <40%, далі зелений.
  // Навантаження: логіка навпаки - зелений <40%, жовтий <70%, далі червоний
  // (низьке навантаження - добре, високе - привід насторожитись).
  if (isLoad) {
    if (percent < 40) return '#6bcb77';
    if (percent < 70) return '#f0c040';
    return '#ff6b6b';
  }
  if (percent < 20) return '#ff6b6b';
  if (percent < 40) return '#f0c040';
  return '#6bcb77';
}

function renderLevelBar(percent, isLoad) {
  const color = levelColor(percent, isLoad);
  const lit = Math.max(0, Math.min(5, Math.round(percent / 20)));
  let segs = '';
  for (let i = 0; i < 5; i++) {
    segs += `<span class="seg" style="background:${i < lit ? color : '#333'}"></span>`;
  }
  return `<span class="bar">${segs}</span><span class="bar-pct">${percent}%</span>`;
}

async function poll() {
  try {
    const r = await fetch('/api/data');
    const d = await r.json();
    document.getElementById('fw').textContent = 'v' + d.version;
    const t2 = document.getElementById('t');
    t2.innerHTML = `<tr><th>${t('th_channel')}</th><th>${t('th_level')}</th><th>${t('th_voltage')}</th><th>${t('th_current')}</th><th>${t('th_power')}</th></tr>`;
    d.channels.forEach(c => {
      const cls = c.current_mA < 0 ? 'neg' : 'pos';
      let level = '';
      if (c.soc_percent !== undefined) level = renderLevelBar(c.soc_percent, false);
      else if (c.load_percent !== undefined) level = renderLevelBar(c.load_percent, true);
      t2.innerHTML += `<tr><td>${c.label}</td><td class="level">${level}</td><td class="val">${c.bus_V.toFixed(2)} ${t('unit_v')}</td><td class="val ${cls}">${(c.current_mA/1000).toFixed(3)} ${t('unit_a')}</td><td class="val ${cls}">${(c.power_mW/1000).toFixed(1)} ${t('unit_w')}</td></tr>`;
    });
    document.getElementById('ts').textContent = new Date().toLocaleTimeString();
  } catch (e) { console.error(e); }
}
async function rebootDevice() {
  if (confirm(t('reboot_confirm'))) {
    try {
      fetch('/api/reboot', {method: 'POST'}).catch(() => {});
      alert(t('reboot_alert'));
      setTimeout(() => { location.reload(); }, 6000);
    } catch (e) {
      alert(t('reboot_sent'));
    }
  }
}
applyLang();
poll();
setInterval(poll, 1000);
</script>
</body>
</html>
)HTML";

// Спільні CSS-стилі для сторінок налаштувань (settings hub + підсторінки)
#define SETTINGS_CSS \
  "body { font-family: -apple-system, sans-serif; background:#111; color:#eee; padding:20px; max-width: 420px; margin: auto; }" \
  "a { text-decoration: none; }" \
  "input, select { width:100%; padding:8px; margin:6px 0 16px 0; box-sizing:border-box; background:#222; border:1px solid #444; color:#fff; border-radius: 4px; }" \
  "label { color: #aaa; font-size: 0.9rem; }" \
  "button { background: #7aa2f7; color: #111; border: none; padding:10px 16px; margin-top:10px; width: 100%; font-weight: bold; border-radius: 4px; cursor: pointer; }" \
  "button:hover { background: #89b4fa; }" \
  ".back { margin-top: 15px; display: block; text-align: center; color: #7aa2f7; text-decoration: none; }" \
  "h1 { display: flex; justify-content: space-between; align-items: center; font-size: 1.3rem; }" \
  ".lang-switch { font-size: 0.75rem; color: #777; }" \
  ".lang-switch a { color: #777; padding: 2px 4px; text-decoration: none; }" \
  ".lang-switch a.active { color: #7aa2f7; font-weight: bold; }" \
  ".hint { color:#888; font-size:0.8rem; margin-top:-10px; margin-bottom:16px; }" \
  ".card-list { list-style: none; padding: 0; margin: 16px 0 0 0; }" \
  ".card-list li { margin-bottom: 10px; }" \
  ".card-list a { display: block; background:#1c1c1c; border:1px solid #333; border-radius:6px; padding:14px; color:#eee; }" \
  ".card-list a:hover { border-color: #7aa2f7; text-decoration: none; }" \
  ".card-list .item-title { font-weight: bold; }" \
  ".card-list .item-desc { color: #888; font-size: 0.8rem; margin-top: 4px; }" \
  ".saved-msg { color: #6bcb77; font-size: 0.85rem; margin-top: -10px; margin-bottom: 10px; display: none; }"

#define LANG_SWITCH_HTML \
  "<span class=\"lang-switch\"><a href=\"#\" id=\"lang-uk\" onclick=\"setLang('uk');return false;\">UA</a>/<a href=\"#\" id=\"lang-en\" onclick=\"setLang('en');return false;\">EN</a></span>"

#define LANG_SWITCH_JS \
  "let lang = localStorage.getItem('lang') || (navigator.language && navigator.language.startsWith('en') ? 'en' : 'uk');" \
  "function t(key) { return I18N[lang][key] || key; }" \
  "function applyLang() {" \
  "  document.documentElement.lang = lang;" \
  "  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = t(el.getAttribute('data-i18n')); });" \
  "  document.getElementById('lang-uk').classList.toggle('active', lang === 'uk');" \
  "  document.getElementById('lang-en').classList.toggle('active', lang === 'en');" \
  "}" \
  "function setLang(l) { lang = l; localStorage.setItem('lang', l); applyLang(); }"

// ---------------------------------------------------------------------------
// Settings hub
// ---------------------------------------------------------------------------
const char SETTINGS_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="title">Налаштування</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<ul class="card-list">
  <li><a href="/wifi"><div class="item-title" data-i18n="item_wifi">WiFi мережа</div><div class="item-desc" data-i18n="item_wifi_desc">SSID та пароль домашньої мережі</div></a></li>
  <li><a href="/settings/password"><div class="item-title" data-i18n="item_password">Пароль доступу</div><div class="item-desc" data-i18n="item_password_desc">Пароль для цих сторінок і OTA</div></a></li>
  <li><a href="/settings/ntp"><div class="item-title" data-i18n="item_ntp">Дата й час (NTP)</div><div class="item-desc" data-i18n="item_ntp_desc">Сервер синхронізації, часовий пояс</div></a></li>
  <li><a href="/settings/battery"><div class="item-title" data-i18n="item_battery">Батарея</div><div class="item-desc" data-i18n="item_battery_desc">Ємність, напруга 100%/0% заряду</div></a></li>
  <li><a href="/settings/history"><div class="item-title" data-i18n="item_history">Зберігання історії</div><div class="item-desc" data-i18n="item_history_desc">Термін зберігання даних для графіків</div></a></li>
</ul>
<a class="back" href="/" data-i18n="back_link">← На головну</a>
<script>
const I18N = {
  uk: {
    title: "Налаштування",
    item_wifi: "WiFi мережа", item_wifi_desc: "SSID та пароль домашньої мережі",
    item_password: "Пароль доступу", item_password_desc: "Пароль для цих сторінок і OTA",
    item_ntp: "Дата й час (NTP)", item_ntp_desc: "Сервер синхронізації, часовий пояс",
    item_battery: "Батарея", item_battery_desc: "Ємність, напруга 100%/0% заряду",
    item_history: "Зберігання історії", item_history_desc: "Термін зберігання даних для графіків",
    back_link: "← На головну"
  },
  en: {
    title: "Settings",
    item_wifi: "WiFi network", item_wifi_desc: "Home network SSID and password",
    item_password: "Access password", item_password_desc: "Password for these pages and OTA",
    item_ntp: "Date & time (NTP)", item_ntp_desc: "Sync server, timezone",
    item_battery: "Battery", item_battery_desc: "Capacity, 100%/0% charge voltage",
    item_history: "History retention", item_history_desc: "How long to keep data for graphs",
    back_link: "← Back to home"
  }
};
)HTML" LANG_SWITCH_JS R"HTML(
applyLang();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// WiFi (лише SSID/пароль мережі - пароль доступу переїхав на окрему сторінку)
// ---------------------------------------------------------------------------
const char WIFI_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi Setup</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="wifi_title">Налаштування WiFi</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<form method="POST" action="/wifi/save">
  <label data-i18n="label_ssid">SSID</label>
  <input name="ssid" required>
  <label data-i18n="label_pass">Пароль</label>
  <input name="pass" type="password">
  <button type="submit" data-i18n="btn_save_wifi">Зберегти й перезавантажити</button>
</form>
<a class="back" href="/settings" data-i18n="back_link">← До налаштувань</a>
<script>
const I18N = {
  uk: {
    wifi_title: "Налаштування WiFi", label_ssid: "SSID", label_pass: "Пароль",
    btn_save_wifi: "Зберегти й перезавантажити",
    back_link: "← До налаштувань"
  },
  en: {
    wifi_title: "WiFi Setup", label_ssid: "SSID", label_pass: "Password",
    btn_save_wifi: "Save & reboot",
    back_link: "← Back to settings"
  }
};
)HTML" LANG_SWITCH_JS R"HTML(
applyLang();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// Пароль доступу
// ---------------------------------------------------------------------------
const char PASSWORD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Password</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="pass_title">Пароль доступу</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<p class="hint" data-i18n="pass_desc">
  Той самий пароль (логін завжди admin) використовується і для цих сторінок, і для OTA-оновлень. Мінімум 8 символів.
</p>
<form method="POST" action="/api/set-password" id="passForm">
  <label data-i18n="label_newpass">Новий пароль</label>
  <input name="newpass" type="password" minlength="8" required>
  <button type="submit" data-i18n="btn_change_pass">Змінити пароль</button>
</form>
<a class="back" href="/settings" data-i18n="back_link">← До налаштувань</a>
<script>
const I18N = {
  uk: {
    pass_title: "Пароль доступу",
    pass_desc: "Той самий пароль (логін завжди admin) використовується і для цих сторінок, і для OTA-оновлень. Мінімум 8 символів.",
    label_newpass: "Новий пароль", btn_change_pass: "Змінити пароль",
    confirm_pass_change: "Зберегти новий пароль і перезавантажити пристрій?",
    back_link: "← До налаштувань"
  },
  en: {
    pass_title: "Access password",
    pass_desc: "The same password (username is always admin) is used for these pages and for OTA updates. Minimum 8 characters.",
    label_newpass: "New password", btn_change_pass: "Change password",
    confirm_pass_change: "Save the new password and reboot the device?",
    back_link: "← Back to settings"
  }
};
)HTML" LANG_SWITCH_JS R"HTML(
document.getElementById('passForm').addEventListener('submit', function(e) {
  if (!confirm(t('confirm_pass_change'))) e.preventDefault();
});
applyLang();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// NTP / час
// ---------------------------------------------------------------------------
const char NTP_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>NTP</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="ntp_title">Дата й час</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<p><span data-i18n="current_time_label">Поточний час пристрою:</span> <b id="curTime">-</b></p>
<form method="POST" action="/settings/ntp/save" id="ntpForm">
  <label data-i18n="label_ntp_server">NTP сервер</label>
  <input name="srv" id="srv" required>
  <label data-i18n="label_tz_preset">Часовий пояс</label>
  <select id="tzPreset"></select>
  <div id="tzManual" style="display:none;">
    <label data-i18n="label_tz">POSIX TZ (вручну)</label>
    <input name="tz" id="tz" required>
    <div class="hint" data-i18n="tz_hint">Приклад для Києва: EET-2EEST,M3.5.0/3,M10.5.0/4</div>
  </div>
  <div class="saved-msg" id="savedMsg" data-i18n="saved_msg">Збережено</div>
  <button type="submit" data-i18n="btn_save">Зберегти</button>
</form>
<a class="back" href="/settings" data-i18n="back_link">← До налаштувань</a>
<script>
const I18N = {
  uk: {
    ntp_title: "Дата й час", current_time_label: "Поточний час пристрою:",
    label_ntp_server: "NTP сервер", label_tz_preset: "Часовий пояс", label_tz: "POSIX TZ (вручну)",
    tz_hint: "Приклад для Києва: EET-2EEST,M3.5.0/3,M10.5.0/4",
    tz_custom: "Інше (вручну)...",
    saved_msg: "Збережено", btn_save: "Зберегти",
    not_synced: "не синхронізовано",
    back_link: "← До налаштувань"
  },
  en: {
    ntp_title: "Date & time", current_time_label: "Device current time:",
    label_ntp_server: "NTP server", label_tz_preset: "Timezone", label_tz: "POSIX TZ (manual)",
    tz_hint: "Example for Kyiv: EET-2EEST,M3.5.0/3,M10.5.0/4",
    tz_custom: "Other (manual)...",
    saved_msg: "Saved", btn_save: "Save",
    not_synced: "not synced yet",
    back_link: "← Back to settings"
  }
};

// Поширені часові пояси. tz - POSIX TZ рядок з правилами DST там, де вони є.
const TZ_PRESETS = [
  { tz: "EET-2EEST,M3.5.0/3,M10.5.0/4", uk: "Київ", en: "Kyiv" },
  { tz: "CET-1CEST,M3.5.0,M10.5.0/3",   uk: "Берлін / Варшава / Париж", en: "Berlin / Warsaw / Paris" },
  { tz: "GMT0BST,M3.5.0/1,M10.5.0",     uk: "Лондон", en: "London" },
  { tz: "MSK-3",                        uk: "Москва", en: "Moscow" },
  { tz: "<+03>-3",                      uk: "Стамбул", en: "Istanbul" },
  { tz: "EST5EDT,M3.2.0,M11.1.0",       uk: "Нью-Йорк", en: "New York" },
  { tz: "PST8PDT,M3.2.0,M11.1.0",       uk: "Лос-Анджелес", en: "Los Angeles" },
  { tz: "UTC0",                         uk: "UTC (без зсуву)", en: "UTC (no offset)" }
];
)HTML" LANG_SWITCH_JS R"HTML(

// Перебудовує список пресетів мовою інтерфейсу, зберігаючи поточний вибір.
function populateTzPresets() {
  const sel = document.getElementById('tzPreset');
  const prevValue = sel.value;
  sel.innerHTML = '';
  TZ_PRESETS.forEach(p => {
    const opt = document.createElement('option');
    opt.value = p.tz;
    opt.textContent = p[lang];
    sel.appendChild(opt);
  });
  const customOpt = document.createElement('option');
  customOpt.value = 'custom';
  customOpt.textContent = t('tz_custom');
  sel.appendChild(customOpt);
  if (prevValue) sel.value = prevValue;
}

// Вибирає у списку пресет, що відповідає поточному значенню #tz, або "custom",
// якщо збережений TZ-рядок не входить у список пресетів - і показує/ховає
// поле ручного вводу відповідно.
function syncTzPresetSelection() {
  const tzVal = document.getElementById('tz').value;
  const sel = document.getElementById('tzPreset');
  const match = TZ_PRESETS.find(p => p.tz === tzVal);
  sel.value = match ? match.tz : 'custom';
  document.getElementById('tzManual').style.display = match ? 'none' : 'block';
}

document.getElementById('tzPreset').addEventListener('change', function() {
  if (this.value === 'custom') {
    document.getElementById('tzManual').style.display = 'block';
  } else {
    document.getElementById('tz').value = this.value;
    document.getElementById('tzManual').style.display = 'none';
  }
});

// Мова могла змінитись - перебудовуємо підписи пресетів поверх стандартного applyLang.
const _origSetLang = setLang;
setLang = function(l) {
  _origSetLang(l);
  populateTzPresets();
};

// Скорочені назви місяців для формату дати на сторінці (uk: "25 серп 2026",
// en: "25 Aug 2026") - навмисно не MM/DD/YYYY і не залежний від toLocaleString(),
// щоб не плутати користувачів різними регіональними форматами дати.
const MONTHS_SHORT = {
  uk: ['січ', 'лют', 'бер', 'кві', 'тра', 'чер', 'лип', 'сер', 'вер', 'жов', 'лис', 'гру'],
  en: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']
};

function formatDateTime(date) {
  const day = date.getDate();
  const month = MONTHS_SHORT[lang][date.getMonth()];
  const year = date.getFullYear();
  const time = date.toLocaleTimeString(lang === 'uk' ? 'uk-UA' : 'en-US');
  return `${day} ${month} ${year}, ${time}`;
}

async function loadTime() {
  try {
    const r = await fetch('/api/data');
    const d = await r.json();
    document.getElementById('curTime').textContent = d.time_synced ? formatDateTime(new Date(d.time * 1000)) : t('not_synced');
  } catch (e) { console.error(e); }
}

async function loadFormValues() {
  try {
    const r2 = await fetch('/api/ntp-settings');
    const d2 = await r2.json();
    document.getElementById('srv').value = d2.srv;
    document.getElementById('tz').value = d2.tz;
    syncTzPresetSelection();
  } catch (e) { console.error(e); }
}

document.getElementById('ntpForm').addEventListener('submit', async function(e) {
  e.preventDefault();
  const fd = new FormData(e.target);
  await fetch('/settings/ntp/save', { method: 'POST', body: fd });
  document.getElementById('savedMsg').style.display = 'block';
  setTimeout(loadTime, 1500);
});

populateTzPresets();
applyLang();
loadFormValues();
loadTime();
setInterval(loadTime, 5000);
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// Батарея
// ---------------------------------------------------------------------------
const char BATTERY_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Battery</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="batt_title">Батарея</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<form method="POST" action="/settings/battery/save" id="battForm">
  <label data-i18n="label_capacity">Ємність (мАг)</label>
  <input name="cap" id="cap" type="number" min="100" step="1" required>
  <label data-i18n="label_v100">Напруга при 100% заряду (В)</label>
  <input name="v100" id="v100" type="number" min="0" step="0.01" required>
  <label data-i18n="label_v0">Напруга при 0% заряду (В)</label>
  <input name="v0" id="v0" type="number" min="0" step="0.01" required>
  <div class="hint" data-i18n="batt_hint">За замовчуванням для LiFePO4 4S2P (4000мАг/елемент): 8000мАг, 13.94В / 10.60В. Оцінка заряду за напругою груба — LiFePO4 має плоску розрядну криву.</div>
  <div class="saved-msg" id="savedMsg" data-i18n="saved_msg">Збережено</div>
  <button type="submit" data-i18n="btn_save">Зберегти</button>
</form>
<a class="back" href="/settings" data-i18n="back_link">← До налаштувань</a>
<script>
const I18N = {
  uk: {
    batt_title: "Батарея",
    label_capacity: "Ємність (мАг)", label_v100: "Напруга при 100% заряду (В)", label_v0: "Напруга при 0% заряду (В)",
    batt_hint: "За замовчуванням для LiFePO4 4S2P (4000мАг/елемент): 8000мАг, 13.94В / 10.60В. Оцінка заряду за напругою груба — LiFePO4 має плоску розрядну криву.",
    saved_msg: "Збережено", btn_save: "Зберегти",
    back_link: "← До налаштувань"
  },
  en: {
    batt_title: "Battery",
    label_capacity: "Capacity (mAh)", label_v100: "Voltage at 100% charge (V)", label_v0: "Voltage at 0% charge (V)",
    batt_hint: "Defaults for a 4S2P LiFePO4 pack (4000mAh/cell): 8000mAh, 13.94V / 10.60V. Voltage-based charge estimation is rough — LiFePO4 has a flat discharge curve.",
    saved_msg: "Saved", btn_save: "Save",
    back_link: "← Back to settings"
  }
};
)HTML" LANG_SWITCH_JS R"HTML(

async function loadCurrent() {
  try {
    const r = await fetch('/api/battery-settings');
    const d = await r.json();
    document.getElementById('cap').value = d.cap;
    document.getElementById('v100').value = d.v100;
    document.getElementById('v0').value = d.v0;
  } catch (e) { console.error(e); }
}

document.getElementById('battForm').addEventListener('submit', async function(e) {
  e.preventDefault();
  const fd = new FormData(e.target);
  await fetch('/settings/battery/save', { method: 'POST', body: fd });
  document.getElementById('savedMsg').style.display = 'block';
});

applyLang();
loadCurrent();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// Зберігання історії (лише налаштування - сама історія буде додана пізніше)
// ---------------------------------------------------------------------------
const char HISTORY_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>History</title>
<style>)HTML" SETTINGS_CSS R"HTML(</style>
</head>
<body>
<h1>
  <span data-i18n="hist_title">Зберігання історії</span>
  )HTML" LANG_SWITCH_HTML R"HTML(
</h1>
<p class="hint" data-i18n="hist_note">Функціонал історії та графіків буде додано пізніше. Це налаштування вже зберігається, щоб не питати повторно.</p>
<form method="POST" action="/settings/history/save" id="histForm">
  <label data-i18n="label_retention">Термін зберігання</label>
  <select name="days" id="days">
    <option value="30" data-i18n="opt_1m">1 місяць</option>
    <option value="90" data-i18n="opt_3m">3 місяці</option>
    <option value="182" data-i18n="opt_6m">6 місяців</option>
    <option value="365" data-i18n="opt_1y">1 рік</option>
  </select>
  <div class="saved-msg" id="savedMsg" data-i18n="saved_msg">Збережено</div>
  <button type="submit" data-i18n="btn_save">Зберегти</button>
</form>
<a class="back" href="/settings" data-i18n="back_link">← До налаштувань</a>
<script>
const I18N = {
  uk: {
    hist_title: "Зберігання історії",
    hist_note: "Функціонал історії та графіків буде додано пізніше. Це налаштування вже зберігається, щоб не питати повторно.",
    label_retention: "Термін зберігання",
    opt_1m: "1 місяць", opt_3m: "3 місяці", opt_6m: "6 місяців", opt_1y: "1 рік",
    saved_msg: "Збережено", btn_save: "Зберегти",
    back_link: "← До налаштувань"
  },
  en: {
    hist_title: "History retention",
    hist_note: "History and graphs are not implemented yet. This setting is already saved so you won't need to set it again later.",
    label_retention: "Retention period",
    opt_1m: "1 month", opt_3m: "3 months", opt_6m: "6 months", opt_1y: "1 year",
    saved_msg: "Saved", btn_save: "Save",
    back_link: "← Back to settings"
  }
};
)HTML" LANG_SWITCH_JS R"HTML(

async function loadCurrent() {
  try {
    const r = await fetch('/api/history-settings');
    const d = await r.json();
    document.getElementById('days').value = d.days;
  } catch (e) { console.error(e); }
}

document.getElementById('histForm').addEventListener('submit', async function(e) {
  e.preventDefault();
  const fd = new FormData(e.target);
  await fetch('/settings/history/save', { method: 'POST', body: fd });
  document.getElementById('savedMsg').style.display = 'block';
});

applyLang();
loadCurrent();
</script>
</body>
</html>
)HTML";
