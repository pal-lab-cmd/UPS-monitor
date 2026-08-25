#pragma once

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
  <tr><th data-i18n="th_channel">Канал</th><th data-i18n="th_voltage">Напруга</th><th data-i18n="th_current">Струм</th><th data-i18n="th_power">Потужність</th></tr>
</table>
<footer>
  <div><span data-i18n="updated_label">Оновлено:</span> <span id="ts">-</span> &nbsp;|&nbsp; <a href="/update" data-i18n="ota_link">OTA оновлення</a> &nbsp;|&nbsp; <a href="/wifi" data-i18n="wifi_link">WiFi та пароль</a></div>
  <button class="btn-restart" onclick="rebootDevice()" data-i18n="reboot_btn">Перезавантажити</button>
</footer>
<script>
const I18N = {
  uk: {
    th_channel: "Канал", th_voltage: "Напруга", th_current: "Струм", th_power: "Потужність",
    updated_label: "Оновлено:", ota_link: "OTA оновлення", wifi_link: "WiFi та пароль",
    reboot_btn: "Перезавантажити",
    reboot_confirm: "Ви дійсно хочете перезавантажити пристрій?",
    reboot_alert: "Пристрій перезавантажується. Сторінка оновиться за кілька секунд...",
    reboot_sent: "Запит відправлено",
    unit_v: "В", unit_a: "А", unit_w: "Вт"
  },
  en: {
    th_channel: "Channel", th_voltage: "Voltage", th_current: "Current", th_power: "Power",
    updated_label: "Updated:", ota_link: "OTA update", wifi_link: "WiFi & password",
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

async function poll() {
  try {
    const r = await fetch('/api/data');
    const d = await r.json();
    document.getElementById('fw').textContent = 'v' + d.version;
    const t2 = document.getElementById('t');
    t2.innerHTML = `<tr><th>${t('th_channel')}</th><th>${t('th_voltage')}</th><th>${t('th_current')}</th><th>${t('th_power')}</th></tr>`;
    d.channels.forEach(c => {
      const cls = c.current_mA < 0 ? 'neg' : 'pos';
      t2.innerHTML += `<tr><td>${c.label}</td><td class="val">${c.bus_V.toFixed(2)} ${t('unit_v')}</td><td class="val ${cls}">${(c.current_mA/1000).toFixed(3)} ${t('unit_a')}</td><td class="val ${cls}">${(c.power_mW/1000).toFixed(1)} ${t('unit_w')}</td></tr>`;
    });
    document.getElementById('ts').textContent = new Date().toLocaleTimeString();
  } catch (e) { console.error(e); }
}
async function rebootDevice() {
  if (confirm(t('reboot_confirm'))) {
    try {
      // Використовуємо fetch, але не чекаємо тіло відповіді, якщо з'єднання впаде
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

const char WIFI_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi Setup</title>
<style>
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; padding:20px; max-width: 400px; margin: auto; }
  input { width:100%; padding:8px; margin:6px 0 16px 0; box-sizing:border-box; background:#222; border:1px solid #444; color:#fff; border-radius: 4px; }
  label { color: #aaa; font-size: 0.9rem; }
  button { background: #7aa2f7; color: #111; border: none; padding:10px 16px; margin-top:10px; width: 100%; font-weight: bold; border-radius: 4px; cursor: pointer; }
  button:hover { background: #89b4fa; }
  .back { margin-top: 15px; display: block; text-align: center; color: #7aa2f7; text-decoration: none; }
  h1 { display: flex; justify-content: space-between; align-items: center; font-size: 1.3rem; }
  .lang-switch { font-size: 0.75rem; color: #777; }
  .lang-switch a { color: #777; padding: 2px 4px; text-decoration: none; }
  .lang-switch a.active { color: #7aa2f7; font-weight: bold; }
</style>
</head>
<body>
<h1>
  <span data-i18n="wifi_title">Налаштування WiFi</span>
  <span class="lang-switch">
    <a href="#" id="lang-uk" onclick="setLang('uk');return false;">UA</a>/<a href="#" id="lang-en" onclick="setLang('en');return false;">EN</a>
  </span>
</h1>
<form method="POST" action="/wifi/save">
  <label data-i18n="label_ssid">SSID</label>
  <input name="ssid" required>
  <label data-i18n="label_pass">Пароль</label>
  <input name="pass" type="password">
  <button type="submit" data-i18n="btn_save_wifi">Зберегти й перезавантажити</button>
</form>

<h1 style="margin-top:32px;"><span data-i18n="pass_title">Пароль доступу</span></h1>
<p style="color:#888; font-size:0.85rem;" data-i18n="pass_desc">
  Той самий пароль (логін завжди admin) використовується і для цієї сторінки,
  і для OTA-оновлень. Мінімум 8 символів.
</p>
<form method="POST" action="/api/set-password" id="passForm">
  <label data-i18n="label_newpass">Новий пароль</label>
  <input name="newpass" type="password" minlength="8" required>
  <button type="submit" data-i18n="btn_change_pass">Змінити пароль</button>
</form>

<a class="back" href="/" data-i18n="back_link">← На головну</a>
<script>
const I18N = {
  uk: {
    wifi_title: "Налаштування WiFi", label_ssid: "SSID", label_pass: "Пароль",
    btn_save_wifi: "Зберегти й перезавантажити",
    pass_title: "Пароль доступу",
    pass_desc: "Той самий пароль (логін завжди admin) використовується і для цієї сторінки, і для OTA-оновлень. Мінімум 8 символів.",
    label_newpass: "Новий пароль", btn_change_pass: "Змінити пароль",
    confirm_pass_change: "Зберегти новий пароль і перезавантажити пристрій?",
    back_link: "← На головну"
  },
  en: {
    wifi_title: "WiFi Setup", label_ssid: "SSID", label_pass: "Password",
    btn_save_wifi: "Save & reboot",
    pass_title: "Access password",
    pass_desc: "The same password (username is always admin) is used for this page and for OTA updates. Minimum 8 characters.",
    label_newpass: "New password", btn_change_pass: "Change password",
    confirm_pass_change: "Save the new password and reboot the device?",
    back_link: "← Back to home"
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
}

document.getElementById('passForm').addEventListener('submit', function(e) {
  if (!confirm(t('confirm_pass_change'))) e.preventDefault();
});

applyLang();
</script>
</body>
</html>
)HTML";