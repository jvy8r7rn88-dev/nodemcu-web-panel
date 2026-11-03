/*
  NodeMCU v3 (ESP8266) Web Panel — демонстрационный веб-сервер с вкладками.

  Как подключиться с телефона:
    1. Прошить плату (Arduino IDE, плата "NodeMCU 1.0 (ESP-12E Module)").
    2. На телефоне подключиться к Wi-Fi "NodeMCU-Panel", пароль "12345678".
    3. Страница откроется сама (captive portal) или зайти на http://192.168.4.1

  Вкладки:
    - Главная    — статус: аптайм, память, IP, сигнал, причина старта
    - Игра       — «Звездопад»: уворачиваться от камней, ловить звёзды;
                   таблица рекордов хранится на плате, при новом рекорде мигает LED
    - Управление — встроенный светодиод (GPIO2 / D4): вкл/выкл и яркость
    - Wi-Fi      — сканирование сетей и подключение к домашнему роутеру
    - Настройки  — имя устройства, имя и пароль точки доступа (хранятся в EEPROM)
    - Система    — информация о чипе, перезагрузка, сброс настроек

  Внешние библиотеки не нужны — только то, что идёт с ядром ESP8266.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

#define LED_PIN 2          // встроенный светодиод на модуле ESP-12 (D4)
#define CFG_MAGIC 0x4E4D   // признак того, что в EEPROM уже лежат настройки
#define BOARD_MAGIC 0x5343 // то же для таблицы рекордов
#define TOP_N 10

ESP8266WebServer server(80);
DNSServer dns;

struct Settings {
  uint16_t magic;
  char devName[32], apSsid[33], apPass[65], staSsid[33], staPass[65];
} cfg;

struct Board {
  uint16_t magic;
  struct { char name[32]; uint32_t score; } e[TOP_N];
} board;

bool ledOn = false;
int ledBright = 255;
unsigned long celebrateUntil = 0;  // мигание LED при новом рекорде

// ---------------------------------------------------------------- страница

const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html lang="ru"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NodeMCU Panel</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--line:#334155;--txt:#e2e8f0;--mut:#94a3b8;--acc:#38bdf8;--ok:#22c55e;--bad:#ef4444}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt)}
header{padding:14px 16px;font-weight:600;font-size:18px;display:flex;justify-content:space-between;align-items:center}
header small{color:var(--mut);font-weight:400;font-size:13px}
nav{display:flex;overflow-x:auto;gap:6px;padding:0 12px 10px;border-bottom:1px solid var(--line);scrollbar-width:none}
nav::-webkit-scrollbar{display:none}
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
#cv{width:100%;height:60vh;display:block;border-radius:8px;background:#020617;touch-action:none}
.hud{display:flex;justify-content:space-between;padding:4px 6px 8px;font-size:14px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--ok);color:#0f172a;padding:10px 18px;border-radius:8px;font-weight:600;opacity:0;transition:.3s;pointer-events:none}
</style></head><body>
<header><span id="hname">NodeMCU</span><small id="hstate">…</small></header>
<nav>
<button data-t="home" class="on">Главная</button><button data-t="game">Игра</button><button data-t="ctrl">Управление</button>
<button data-t="wifi">Wi-Fi</button><button data-t="set">Настройки</button><button data-t="sys">Система</button>
</nav>
<main>

<section class="tab on" id="home">
 <div class="card"><h3>Состояние</h3><div class="grid">
  <div class="stat"><span>Аптайм</span><b id="up">—</b></div>
  <div class="stat"><span>Свободно RAM</span><b id="heap">—</b></div>
  <div class="stat"><span>Клиентов на AP</span><b id="cli">—</b></div>
  <div class="stat"><span>Причина старта</span><b id="rst" style="font-size:14px">—</b></div>
 </div></div>
 <div class="card"><h3>Сеть</h3>
  <div class="row"><span>Точка доступа</span><span id="apip">—</span></div>
  <div class="row"><span>Роутер</span><span id="sta">—</span></div>
  <div class="row"><span>Сигнал</span><span id="rssi">—</span></div>
 </div>
</section>

<section class="tab" id="game">
 <div class="card" style="padding:8px">
  <div class="hud"><span>Очки: <b id="gs">0</b></span><span id="gl" style="color:var(--bad)">♥♥♥</span><span>Рекорд: <b id="gr">—</b></span></div>
  <canvas id="cv"></canvas>
  <div id="gover" style="display:none;padding:6px">
   <label>Ваш результат: <b id="gfin"></b>. Имя для таблицы:</label>
   <input type="text" id="gname" maxlength="10">
   <button class="btn" id="gsend" onclick="sendScore()">Записать на плату</button>
   <button class="btn gray" onclick="newGame()">Ещё раз</button>
  </div>
 </div>
 <div class="card"><h3>Рекорды (хранятся на плате)</h3><div id="lb" style="font-size:14px;color:var(--mut)">—</div></div>
</section>

<section class="tab" id="ctrl">
 <div class="card"><h3>Светодиод (GPIO2 / D4)</h3>
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
 <div class="card"><h3>Точка доступа</h3>
  <label>Имя сети</label><input type="text" id="as">
  <label>Пароль (мин. 8 символов, пусто — не менять)</label><input type="password" id="ap">
  <button class="btn" onclick="saveSet()">Сохранить и перезагрузить</button>
 </div>
</section>

<section class="tab" id="sys">
 <div class="card"><h3>Чип</h3><div id="info"></div></div>
 <div class="card">
  <button class="btn gray" onclick="act('reboot','Перезагрузить плату?')">Перезагрузить</button>
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
 if(b.dataset.t=='game')showGame();
});
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'д ':'')+h+'ч '+m+'м '+(s%60)+'с'}
let busy=false;
async function poll(){
 try{const j=await (await fetch('/api/status')).json();
  $('hname').textContent=j.name;$('up').textContent=fmtUp(j.uptime);
  $('heap').textContent=(j.heap/1024).toFixed(1)+' КБ';$('rst').textContent=j.reset;
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

// ---- игра «Звездопад»
const cv=$('cv'),cx=cv.getContext('2d');let G=null,W=0,H=0,last=0,raf=0;
const bg=Array.from({length:40},()=>({x:Math.random(),y:Math.random(),s:Math.random()*1.5+.5}));
const onGame=()=>$('game').classList.contains('on');
function fit(){const r=cv.getBoundingClientRect(),d=devicePixelRatio||1;cv.width=r.width*d;cv.height=r.height*d;cx.setTransform(d,0,0,d,0,0);W=r.width;H=r.height}
function showGame(){fit();loadLb();if(G&&!G.over){last=performance.now();cancelAnimationFrame(raf);raf=requestAnimationFrame(loop)}else if(!G)idle();else draw()}
function idle(){drawBg(0);cx.fillStyle='#e2e8f0';cx.textAlign='center';cx.font='bold 20px system-ui';cx.fillText('Коснитесь, чтобы начать',W/2,H/2-10);
 cx.font='14px system-ui';cx.fillStyle='#94a3b8';cx.fillText('Водите пальцем: уворачивайтесь от камней,',W/2,H/2+18);cx.fillText('ловите звёзды (+50)',W/2,H/2+38)}
function newGame(){fit();G={x:W/2,t:0,score:0,lives:3,inv:0,objs:[],spawn:0,over:false};$('gover').style.display='none';$('gsend').disabled=false;
 last=performance.now();cancelAnimationFrame(raf);raf=requestAnimationFrame(loop)}
function move(e){if(G&&!G.over){const r=cv.getBoundingClientRect();G.x=Math.max(12,Math.min(W-12,e.clientX-r.left))}}
cv.onpointerdown=e=>{if(!G)newGame();move(e)};cv.onpointermove=move;
const buzz=ms=>{try{navigator.vibrate&&navigator.vibrate(ms)}catch(e){}};
function loop(now){const dt=Math.min(.05,(now-last)/1000);last=now;update(dt);draw();if(!G.over&&onGame())raf=requestAnimationFrame(loop)}
function update(dt){const sp=120+G.t*8,py=H-30;G.t+=dt;G.score+=dt*10;G.inv-=dt;G.spawn-=dt;
 if(G.spawn<=0){G.spawn=Math.max(.22,.9-G.t*.015);const st=Math.random()<.18;
  G.objs.push({x:Math.random()*W,y:-20,r:st?10:10+Math.random()*16,st,v:sp*(.7+Math.random()*.6),a:Math.random()*6})}
 for(const o of G.objs){o.y+=o.v*dt;o.a+=dt*2;const dx=o.x-G.x,dy=o.y-py;
  if(dx*dx+dy*dy<(o.r+12)**2){if(o.st){G.score+=50;o.dead=1;buzz(15)}
   else if(G.inv<=0){G.lives--;G.inv=1.2;o.dead=1;buzz(120);if(G.lives<=0)return gameOver()}}}
 G.objs=G.objs.filter(o=>!o.dead&&o.y<H+40);
 $('gs').textContent=Math.floor(G.score);$('gl').textContent='♥'.repeat(Math.max(0,G.lives))}
function drawBg(dt){cx.fillStyle='#020617';cx.fillRect(0,0,W,H);cx.fillStyle='#475569';
 for(const s of bg){s.y=(s.y+dt*s.s*.15)%1;cx.fillRect(s.x*W,s.y*H,s.s,s.s)}}
function star(x,y,r,a){cx.beginPath();for(let i=0;i<10;i++){const q=a+i*Math.PI/5,rr=i%2?r*.45:r;cx.lineTo(x+Math.cos(q)*rr,y+Math.sin(q)*rr)}cx.closePath();cx.fill()}
function draw(){drawBg(G.over?0:.016*(1+G.t/20));
 for(const o of G.objs){if(o.st){cx.fillStyle='#facc15';star(o.x,o.y,o.r,o.a)}
  else{cx.fillStyle='#64748b';cx.strokeStyle='#334155';cx.lineWidth=3;cx.beginPath();cx.arc(o.x,o.y,o.r,0,7);cx.fill();cx.stroke()}}
 if(!(G.inv>0&&Math.floor(G.t*12)%2)){const y=H-30;cx.fillStyle='#38bdf8';cx.beginPath();cx.moveTo(G.x,y-16);cx.lineTo(G.x-13,y+12);cx.lineTo(G.x,y+6);cx.lineTo(G.x+13,y+12);cx.closePath();cx.fill()}
 if(G.over){cx.fillStyle='rgba(2,6,23,.6)';cx.fillRect(0,0,W,H);cx.fillStyle='#e2e8f0';cx.textAlign='center';cx.font='bold 24px system-ui';
  cx.fillText('Игра окончена',W/2,H/2-8);cx.font='16px system-ui';cx.fillText(Math.floor(G.score)+' очков',W/2,H/2+20)}}
function gameOver(){G.over=true;draw();$('gl').textContent='';$('gfin').textContent=Math.floor(G.score);
 try{$('gname').value=localStorage.getItem('gname')||''}catch(e){}$('gover').style.display='block'}
function sendScore(){const n=$('gname').value.trim()||'Аноним';try{localStorage.setItem('gname',n)}catch(e){}
 $('gsend').disabled=true;post('/api/scores',{n,s:Math.floor(G.score)}).then(r=>r.json()).then(j=>{
  toast(j.rank==1?'🏆 Новый рекорд! Смотрите на плату':j.rank?'Место #'+j.rank+' в таблице':'В топ-10 не попали');loadLb()})}
async function loadLb(){const a=await (await fetch('/api/scores')).json(),lb=$('lb');lb.innerHTML='';
 $('gr').textContent=a.length?a[0].s:'—';if(!a.length)lb.textContent='Пока пусто — станьте первым!';
 a.forEach((x,i)=>{const r=document.createElement('div');r.className='row';const n=document.createElement('span'),s=document.createElement('span');
  n.textContent=(i?i+1+'. ':'🏆 ')+x.n;s.textContent=x.s;r.append(n,s);lb.append(r)})}
window.onresize=()=>{if(onGame()){fit();G?draw():idle()}};

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

void setStr(char* dst, size_t size, const String& src) {
  strncpy(dst, src.c_str(), size - 1);
  dst[size - 1] = 0;
}

void defaultSettings() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  setStr(cfg.devName, sizeof(cfg.devName), "nodemcu");
  setStr(cfg.apSsid,  sizeof(cfg.apSsid),  "NodeMCU-Panel");
  setStr(cfg.apPass,  sizeof(cfg.apPass),  "12345678");
}

void loadSettings() {
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) defaultSettings();
}

void saveSettings() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadBoard() {
  EEPROM.get(sizeof(Settings), board);
  if (board.magic != BOARD_MAGIC) {
    memset(&board, 0, sizeof(board));
    board.magic = BOARD_MAGIC;
  }
}

void saveBoard() {
  EEPROM.put(sizeof(Settings), board);
  EEPROM.commit();
}

// Светодиод на ESP-12 подключён к питанию, поэтому горит при низком уровне
void applyLed() {
  analogWrite(LED_PIN, 255 - (ledOn ? ledBright : 0));
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
  j += "\"reset\":\"" + jsonEsc(ESP.getResetReason()) + "\",";
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
  if (server.arg("name").length())   setStr(cfg.devName, sizeof(cfg.devName), server.arg("name"));
  if (server.arg("apSsid").length()) setStr(cfg.apSsid,  sizeof(cfg.apSsid),  server.arg("apSsid"));
  String p = server.arg("apPass");
  if (p.length() >= 8) setStr(cfg.apPass, sizeof(cfg.apPass), p);
  saveSettings();
  rebootSoon();
}

void handleWifi() {
  setStr(cfg.staSsid, sizeof(cfg.staSsid), server.arg("ssid"));
  setStr(cfg.staPass, sizeof(cfg.staPass), server.arg("pass"));
  saveSettings();
  server.send(200, "text/plain", "ok");
  WiFi.disconnect();
  if (cfg.staSsid[0]) WiFi.begin(cfg.staSsid, cfg.staPass);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + String(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "true" : "false") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

void handleInfo() {
  String j = "{";
  j += "\"Модель\":\"ESP8266 (ID " + String(ESP.getChipId(), HEX) + ")\",";
  j += "\"Частота\":\"" + String(ESP.getCpuFreqMHz()) + " МГц\",";
  j += "\"Flash\":\"" + String(ESP.getFlashChipRealSize() / 1048576) + " МБ\",";
  j += "\"Скетч\":\"" + String(ESP.getSketchSize() / 1024) + " КБ\",";
  j += "\"MAC\":\"" + WiFi.macAddress() + "\",";
  j += "\"Ядро\":\"" + jsonEsc(ESP.getCoreVersion()) + "\",";
  j += "\"SDK\":\"" + String(ESP.getSdkVersion()) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void handleGetScores() {
  String j = "[";
  for (int i = 0; i < TOP_N && board.e[i].name[0]; i++) {
    if (i) j += ",";
    j += "{\"n\":\"" + jsonEsc(board.e[i].name) + "\",\"s\":" + String(board.e[i].score) + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleAddScore() {
  long s = constrain(server.arg("s").toInt(), 0L, 9999999L);
  String n = server.arg("n");
  n.trim();
  if (!n.length()) n = "Аноним";

  int pos = 0;
  while (pos < TOP_N && board.e[pos].name[0] && board.e[pos].score >= (uint32_t)s) pos++;
  int rank = 0;
  if (s > 0 && pos < TOP_N) {
    memmove(&board.e[pos + 1], &board.e[pos], sizeof(board.e[0]) * (TOP_N - 1 - pos));
    setStr(board.e[pos].name, sizeof(board.e[pos].name), n);
    board.e[pos].score = s;
    saveBoard();
    rank = pos + 1;
    if (rank == 1) celebrateUntil = millis() + 3000;
  }
  server.send(200, "application/json", "{\"rank\":" + String(rank) + "}");
}

void handleReset() {
  defaultSettings();
  saveSettings();
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
  EEPROM.begin(sizeof(Settings) + sizeof(Board));
  pinMode(LED_PIN, OUTPUT);
  analogWriteRange(255);
  applyLed();
  loadSettings();
  loadBoard();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(cfg.apSsid, strlen(cfg.apPass) >= 8 ? cfg.apPass : nullptr);
  if (cfg.staSsid[0]) WiFi.begin(cfg.staSsid, cfg.staPass);

  dns.start(53, "*", WiFi.softAPIP());
  MDNS.begin(cfg.devName);
  MDNS.addService("http", "tcp", 80);

  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/status",   HTTP_GET,  handleStatus);
  server.on("/api/led",      HTTP_POST, handleLed);
  server.on("/api/settings", HTTP_GET,  handleGetSettings);
  server.on("/api/settings", HTTP_POST, handleSetSettings);
  server.on("/api/wifi",     HTTP_POST, handleWifi);
  server.on("/api/scan",     HTTP_GET,  handleScan);
  server.on("/api/info",     HTTP_GET,  handleInfo);
  server.on("/api/scores",   HTTP_GET,  handleGetScores);
  server.on("/api/scores",   HTTP_POST, handleAddScore);
  server.on("/api/reboot",   HTTP_POST, rebootSoon);
  server.on("/api/reset",    HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("\nAP: %s  ->  http://%s\n", cfg.apSsid, WiFi.softAPIP().toString().c_str());
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  MDNS.update();

  if (celebrateUntil) {
    if (millis() > celebrateUntil) { celebrateUntil = 0; applyLed(); }
    else analogWrite(LED_PIN, (millis() / 100) % 2 ? 0 : 255);
  }
}
