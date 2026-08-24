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
</style>
</head>
<body>
<h1>
  <span>UPS Monitor</span>
  <span style="font-size: 0.8rem; color: #777;" id="fw">v-</span>
</h1>
<table id="t">
  <tr><th>Канал</th><th>Напруга</th><th>Струм</th></tr>
</table>
<footer>
  <div>Оновлено: <span id="ts">-</span> &nbsp;|&nbsp; <a href="/update">OTA оновлення</a> &nbsp;|&nbsp; <a href="/wifi">WiFi</a></div>
  <button class="btn-restart" onclick="rebootDevice()">Перезавантажити</button>
</footer>
<script>
async function poll() {
  try {
    const r = await fetch('/api/data');
    const d = await r.json();
    document.getElementById('fw').textContent = 'v' + d.version;
    const t = document.getElementById('t');
    t.innerHTML = '<tr><th>Канал</th><th>Напруга</th><th>Струм</th></tr>';
    d.channels.forEach(c => {
      const cls = c.current_mA < 0 ? 'neg' : 'pos';
      t.innerHTML += `<tr><td>${c.label}</td><td class="val">${c.bus_V.toFixed(2)} В</td><td class="val ${cls}">${(c.current_mA/1000).toFixed(3)} А</td></tr>`;
    });
    document.getElementById('ts').textContent = new Date().toLocaleTimeString();
  } catch (e) { console.error(e); }
}
async function rebootDevice() {
  if (confirm("Ви дійсно хочете перезавантажити пристрій?")) {
    try {
      // Використовуємо fetch, але не чекаємо тіло відповіді, якщо з'єднання впаде
      fetch('/api/reboot', {method: 'POST'}).catch(() => {});
      alert("Пристрій перезавантажується. Сторінка оновиться за кілька секунд...");
      setTimeout(() => { location.reload(); }, 6000);
    } catch (e) {
      alert("Запит відправлено");
    }
  }
}
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
  input { width:100%; padding:8px; margin:6px 0 16px 0; box-sizing:border-box; background:#222; border:1px solid #44, color:#eee; color:#fff; border-radius: 4px; }
  label { color: #aaa; font-size: 0.9rem; }
  button { background: #7aa2f7; color: #111; border: none; padding:10px 16px; margin-top:10px; width: 100%; font-weight: bold; border-radius: 4px; cursor: pointer; }
  button:hover { background: #89b4fa; }
  .back { margin-top: 15px; display: block; text-align: center; color: #7aa2f7; text-decoration: none; }
</style>
</head>
<body>
<h1>Налаштування WiFi</h1>
<form method="POST" action="/wifi/save">
  <label>SSID</label>
  <input name="ssid" required>
  <label>Пароль</label>
  <input name="pass" type="password">
  <button type="submit">Зберегти й перезавантажити</button>
</form>
<a class="back" href="/">← На головну</a>
</body>
</html>
)HTML";