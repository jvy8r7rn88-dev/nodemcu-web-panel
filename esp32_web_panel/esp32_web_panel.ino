/*
  ESP32 Web Panel — демонстрационный веб-сервер с вкладками.

  Как подключиться с телефона:
    1. Прошить плату (Arduino IDE, плата "ESP32 Dev Module").
    2. На телефоне подключиться к Wi-Fi "ESP32-Panel", пароль "12345678".
    3. Страница откроется сама (captive portal) или зайти на http://192.168.4.1

  Вкладки:
    - Главная    — статус: аптайм, память, IP, сигнал, температура чипа
    - Управление — встроенный светодиод (GPIO2): вкл/выкл и яркость
    - Wi-Fi      — сканирование сетей и подключение ESP32 к домашнему роутеру
    - Настройки  — имя устройства, имя и пароль точки доступа (хранятся в NVS)
    - Система    — информация о чипе, перезагрузка, сброс настроек

  Внешние библиотеки не нужны — только то, что идёт с ядром ESP32.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#define LED_PIN 2  // встроенный светодиод на большинстве DevKit-плат

WebServer server(80);
DNSServer dns;
Preferences prefs;

struct Settings {
  String devName, apSsid, apPass, staSsid, staPass;
} cfg;

bool ledOn = false;
int ledBright = 255;

// ---------------------------------------------------------------- страница

const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html lang="ru"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Panel</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--line:#334155;--txt:#e2e8f0;--mut:#94a3b8;--acc:#38bdf8;--ok:#22c55e;--bad:#ef4444}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt)}
header{padding:14px 16px;font-weight:600;font-size:18px;display:flex;justify-content:space-between;align-items:center}
header small{color:var(--mut);font-weight:400;font-size:13px}
nav{display:flex;overflow-x:auto;gap:6px;padding:0 12px 10px;border-bottom:1px solid var(--line)}
nav button{flex:none;background:none;border:1px solid var(--line);color:var(--mut);padding:8px 14px;border-radius:20px;font-size:14px}
nav button.on{background:var(--acc);border-color:var(--acc);color:#0f172a;font-weight:600}
main{padding:14px 12px;max-width:640px;margin:auto}
.tab{display:none}.tab.on{display:block}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:12px}
.card h3{margin:0 0 10px;font-size:15px;color:var(--mut);font-weight:500}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.stat b{display:block;font-size:20px;margin-top:2px}.stat span{color:var(--mut);font-size:12px}
label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}
input[type=text],input[type=password]{width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--txt);font-size:15px}
input[type=range]{width:100%}
.btn{width:100%;padding:12px;border:0;border-radius:8px;background:var(--acc);color:#0f172a;font-size:15px;font-weight:600;margin-top:12px}
.btn.red{background:var(--bad);color:#fff}.btn.gray{background:var(--line);color:var(--txt)}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid var(--line);font-size:14px}
.row:last-child{border:0}.row span:first-child{color:var(--mut)}
.sw{position:relative;width:52px;height:30px}.sw input{display:none}
.sw i{position:absolute;inset:0;background:var(--line);border-radius:15px;transition:.2s}
.sw i:before{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:before{transform:translateX(22px)}
.net{display:flex;justify-content:space-between;padding:10px;border-radius:8px;background:var(--bg);margin-top:6px;font-size:14px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--ok);color:#0f172a;padding:10px 18px;border-radius:8px;font-weight:600;opacity:0;transition:.3s;pointer-events:none}
</style></head><body>
<header><span id="hname">ESP32</span><small id="hstate">…</small></header>
<nav>
<button data-t="home" class="on">Главная</button><button data-t="ctrl">Управление</button>
<button data-t="wifi">Wi-Fi</button><button data-t="set">Настройки</button><button data-t="sys">Система</button>
</nav>
<main>

<section class="tab on" id="home">
 <div class="card"><h3>Состояние</h3><div class="grid">
  <div class="stat"><span>Аптайм</span><b id="up">—</b></div>
  <div class="stat"><span>Свободно RAM</span><b id="heap">—</b></div>
  <div class="stat"><span>Температура чипа</span><b id="temp">—</b></div>
  <div class="stat"><span>Клиентов на AP</span><b id="cli">—</b></div>
 </div></div>
 <div class="card"><h3>Сеть</h3>
  <div class="row"><span>Точка доступа</span><span id="apip">—</span></div>
  <div class="row"><span>Роутер</span><span id="sta">—</span></div>
  <div class="row"><span>Сигнал</span><span id="rssi">—</span></div>
 </div>
</section>

<section class="tab" id="ctrl">
 <div class="card"><h3>Светодиод (GPIO2)</h3>
  <div class="row"><span>Включён</span><label class="sw"><input type="checkbox" id="led"><i></i></label></div>
  <label>Яркость: <b id="bv">255</b></label><input type="range" id="br" min="0" max="255" value="255">
 </div>
</section>

<section class="tab" id="wifi">
 <div class="card"><h3>Подключение к роутеру</h3>
  <label>Имя сети (SSID)</label><input type="text" id="ss">
  <label>Пароль</label><input type="password" id="sp">
  <button class="btn" onclick="saveWifi()">Подключиться</button>
 </div>
 <div class="card"><h3>Доступные сети</h3>
  <div id="nets" style="color:var(--mut);font-size:14px">Нажмите «Сканировать»</div>
  <button class="btn gray" id="scanb" onclick="scan()">Сканировать</button>
 </div>
</section>

<section class="tab" id="set">
 <div class="card"><h3>Устройство</h3>
  <label>Имя устройства (также адрес http://имя.local)</label><input type="text" id="dn">
 </div>
 <div class="card"><h3>Точка доступа ESP32</h3>
  <label>Имя сети</label><input type="text" id="as">
  <label>Пароль (мин. 8 символов, пусто — открытая)</label><input type="password" id="ap">
  <button class="btn" onclick="saveSet()">Сохранить и перезагрузить</button>
 </div>
</section>

<section class="tab" id="sys">
 <div class="card"><h3>Чип</h3><div id="info"></div></div>
 <div class="card">
  <button class="btn gray" onclick="act('reboot','Перезагрузить ESP32?')">Перезагрузить</button>
  <button class="btn red" onclick="act('reset','Сбросить все настройки?')">Сброс к заводским</button>
 </div>
</section>

</main><div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const toast=(m,c)=>{const t=$('toast');t.textContent=m;t.style.background=c||'var(--ok)';t.style.opacity=1;setTimeout(()=>t.style.opacity=0,2000)};
const post=(u,d)=>fetch(u,{method:'POST',body:new URLSearchParams(d)});
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
 document.querySelectorAll('nav button,.tab').forEach(e=>e.classList.remove('on'));
 b.classList.add('on');$(b.dataset.t).classList.add('on');
 if(b.dataset.t=='sys')loadInfo();
});
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'д ':'')+h+'ч '+m+'м '+(s%60)+'с'}
let busy=false;
async function poll(){
 try{const j=await (await fetch('/api/status')).json();
  $('hname').textContent=j.name;$('up').textContent=fmtUp(j.uptime);
  $('heap').textContent=(j.heap/1024).toFixed(0)+' КБ';$('temp').textContent=j.temp.toFixed(1)+' °C';
  $('cli').textContent=j.clients;$('apip').textContent=j.apip;
  $('sta').textContent=j.sta?j.ssid+' ('+j.staip+')':'не подключён';
  $('rssi').textContent=j.sta?j.rssi+' dBm':'—';
  $('hstate').textContent=j.sta?'● онлайн':'● только AP';$('hstate').style.color=j.sta?'var(--ok)':'var(--mut)';
  if(!busy){$('led').checked=j.led;$('br').value=j.bright;$('bv').textContent=j.bright}
 }catch(e){$('hstate').textContent='● нет связи';$('hstate').style.color='var(--bad)'}
}
function sendLed(){busy=true;post('/api/led',{on:$('led').checked?1:0,b:$('br').value}).finally(()=>busy=false)}
$('led').onchange=sendLed;
$('br').oninput=()=>{$('bv').textContent=$('br').value;sendLed()};
async function loadSet(){const j=await (await fetch('/api/settings')).json();
 $('dn').value=j.name;$('as').value=j.apSsid;$('ss').value=j.staSsid}
function saveSet(){const p=$('ap').value;if(p&&p.length<8)return toast('Пароль AP минимум 8 символов','var(--bad)');
 post('/api/settings',{name:$('dn').value,apSsid:$('as').value,apPass:p}).then(()=>toast('Сохранено, перезагрузка…'))}
function saveWifi(){post('/api/wifi',{ssid:$('ss').value,pass:$('sp').value}).then(()=>toast('Подключаюсь… проверьте «Главную»'))}
async function scan(){$('scanb').textContent='Сканирую…';
 const n=await (await fetch('/api/scan')).json();$('scanb').textContent='Сканировать';
 $('nets').innerHTML=n.length?'':'Сети не найдены';
 n.sort((a,b)=>b.rssi-a.rssi).forEach(x=>{const d=document.createElement('div');d.className='net';
  const c=x.rssi>-60?'var(--ok)':x.rssi>-75?'#eab308':'var(--bad)';
  d.innerHTML=`<span><i class="dot" style="background:${c}"></i></span><span>${x.rssi} dBm ${x.open?'':'🔒'}</span>`;
  d.firstChild.append(x.ssid);d.onclick=()=>{$('ss').value=x.ssid;$('sp').focus()};$('nets').append(d)})}
async function loadInfo(){const j=await (await fetch('/api/info')).json();
 $('info').innerHTML=Object.entries(j).map(([k,v])=>`<div class="row"><span>${k}</span><span>${v}</span></div>`).join('')}
function act(a,q){if(confirm(q))post('/api/'+a,{}).then(()=>toast('Готово, перезагрузка…'))}
poll();loadSet();setInterval(poll,2000);
</script></body></html>
)rawliteral";

// ---------------------------------------------------------------- утилиты

String jsonEsc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

void loadSettings() {
  prefs.begin("panel", true);
  cfg.devName = prefs.getString("dev", "esp32");
  cfg.apSsid  = prefs.getString("apS", "ESP32-Panel");
  cfg.apPass  = prefs.getString("apP", "12345678");
  cfg.staSsid = prefs.getString("stS", "");
  cfg.staPass = prefs.getString("stP", "");
  prefs.end();
}

void saveSettings() {
  prefs.begin("panel", false);
  prefs.putString("dev", cfg.devName);
  prefs.putString("apS", cfg.apSsid);
  prefs.putString("apP", cfg.apPass);
  prefs.putString("stS", cfg.staSsid);
  prefs.putString("stP", cfg.staPass);
  prefs.end();
}

void applyLed() {
  analogWrite(LED_PIN, ledOn ? ledBright : 0);
}

void rebootSoon() {
  server.send(200, "text/plain", "ok");
  delay(500);
  ESP.restart();
}

// ---------------------------------------------------------------- обработчики

void handleStatus() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String j = "{";
  j += "\"name\":\"" + jsonEsc(cfg.devName) + "\",";
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"temp\":" + String(temperatureRead(), 1) + ",";
  j += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  j += "\"apip\":\"" + WiFi.softAPIP().toString() + "\",";
  j += "\"sta\":" + String(sta ? "true" : "false") + ",";
  j += "\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\",";
  j += "\"staip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"rssi\":" + String(sta ? WiFi.RSSI() : 0) + ",";
  j += "\"led\":" + String(ledOn ? "true" : "false") + ",";
  j += "\"bright\":" + String(ledBright);
  j += "}";
  server.send(200, "application/json", j);
}

void handleLed() {
  ledOn = server.arg("on") == "1";
  ledBright = constrain(server.arg("b").toInt(), 0, 255);
  applyLed();
  server.send(200, "text/plain", "ok");
}

void handleGetSettings() {
  String j = "{\"name\":\"" + jsonEsc(cfg.devName) +
             "\",\"apSsid\":\"" + jsonEsc(cfg.apSsid) +
             "\",\"staSsid\":\"" + jsonEsc(cfg.staSsid) + "\"}";
  server.send(200, "application/json", j);
}

void handleSetSettings() {
  if (server.arg("name").length())   cfg.devName = server.arg("name");
  if (server.arg("apSsid").length()) cfg.apSsid  = server.arg("apSsid");
  String p = server.arg("apPass");
  if (p.length() >= 8) cfg.apPass = p;
  saveSettings();
  rebootSoon();
}

void handleWifi() {
  cfg.staSsid = server.arg("ssid");
  cfg.staPass = server.arg("pass");
  saveSettings();
  server.send(200, "text/plain", "ok");
  WiFi.disconnect();
  if (cfg.staSsid.length()) WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.c_str());
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

void handleInfo() {
  String j = "{";
  j += "\"Модель\":\"" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\",";
  j += "\"Ядер\":" + String(ESP.getChipCores()) + ",";
  j += "\"Частота\":\"" + String(ESP.getCpuFreqMHz()) + " МГц\",";
  j += "\"Flash\":\"" + String(ESP.getFlashChipSize() / 1048576) + " МБ\",";
  j += "\"Скетч\":\"" + String(ESP.getSketchSize() / 1024) + " КБ\",";
  j += "\"MAC\":\"" + WiFi.macAddress() + "\",";
  j += "\"SDK\":\"" + String(ESP.getSdkVersion()) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void handleReset() {
  prefs.begin("panel", false);
  prefs.clear();
  prefs.end();
  rebootSoon();
}

// Любой неизвестный адрес -> на главную (так телефон сам показывает страницу входа)
void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  applyLed();
  loadSettings();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.length() >= 8 ? cfg.apPass.c_str() : nullptr);
  if (cfg.staSsid.length()) WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.c_str());

  dns.start(53, "*", WiFi.softAPIP());
  MDNS.begin(cfg.devName.c_str());
  MDNS.addService("http", "tcp", 80);

  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/status",   HTTP_GET,  handleStatus);
  server.on("/api/led",      HTTP_POST, handleLed);
  server.on("/api/settings", HTTP_GET,  handleGetSettings);
  server.on("/api/settings", HTTP_POST, handleSetSettings);
  server.on("/api/wifi",     HTTP_POST, handleWifi);
  server.on("/api/scan",     HTTP_GET,  handleScan);
  server.on("/api/info",     HTTP_GET,  handleInfo);
  server.on("/api/reboot",   HTTP_POST, rebootSoon);
  server.on("/api/reset",    HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("\nAP: %s  ->  http://%s\n", cfg.apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
}
