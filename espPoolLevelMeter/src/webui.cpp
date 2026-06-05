#include "webui.h"

static const char UI_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PoolLevel Setup</title>
<style>
:root{
  --bg:#0f1117;--surface:#1a1d27;--s2:#22263a;--border:#2e3348;
  --text:#e8eaf0;--muted:#7880a0;--faint:#404660;
  --accent:#00b4cc;--ok:#22c55e;--err:#ef4444;--r:10px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
     font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;padding:16px}
header{display:flex;align-items:center;gap:12px;margin-bottom:20px;
       padding-bottom:14px;border-bottom:1px solid var(--border)}
.logo{width:38px;height:38px}
h1{font-size:1.2rem;font-weight:700}
h1 small{display:block;color:var(--muted);font-size:.78rem;font-weight:400;margin-top:2px}
.tabs{display:flex;gap:4px;background:var(--surface);border:1px solid var(--border);
      border-radius:var(--r);padding:4px;margin-bottom:18px}
.tab{flex:1;padding:8px 2px;border:none;background:transparent;color:var(--muted);
     cursor:pointer;border-radius:8px;font-size:.82rem;font-weight:500;transition:all .15s}
.tab.active{background:var(--s2);color:var(--text)}
.tab:hover:not(.active){color:var(--text)}
.pane{display:none}.pane.active{display:block}
.card{background:var(--surface);border:1px solid var(--border);
      border-radius:var(--r);padding:16px;margin-bottom:14px}
.card-title{font-size:.88rem;font-weight:600;color:var(--accent);margin-bottom:12px}
label{display:block;font-size:.78rem;color:var(--muted);margin-top:10px;margin-bottom:4px;font-weight:500}
label:first-of-type{margin-top:0}
input,select{width:100%;padding:8px 11px;background:var(--s2);border:1px solid var(--border);
             border-radius:7px;color:var(--text);font-size:.88rem;outline:none;transition:border-color .15s}
input:focus,select:focus{border-color:var(--accent)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.chk{display:flex;align-items:center;gap:7px;margin-top:10px}
.chk input{width:auto}.chk label{margin:0;color:var(--text)}
.btn{display:flex;align-items:center;justify-content:center;gap:6px;
     padding:9px 18px;border:none;border-radius:7px;font-size:.88rem;
     font-weight:600;cursor:pointer;transition:all .15s;width:100%;margin-top:10px}
.btn-p{background:var(--accent);color:#000}.btn-p:hover{filter:brightness(1.1)}
.btn-s{background:var(--s2);color:var(--text);border:1px solid var(--border)}
.btn-d{background:var(--err);color:#fff}
.btns{display:flex;gap:8px}.btns .btn{margin-top:0}
.msg{padding:9px 13px;border-radius:7px;font-size:.83rem;margin-top:8px;display:none}
.msg.ok{background:#22c55e18;color:var(--ok);border:1px solid #22c55e33}
.msg.err{background:#ef444418;color:var(--err);border:1px solid #ef444433}
.info-row{display:flex;justify-content:space-between;align-items:center;
          padding:9px 13px;background:var(--s2);border:1px solid var(--border);
          border-radius:7px;margin-bottom:7px;font-size:.83rem}
.info-row .lbl{color:var(--muted)}
.chip{display:inline-block;padding:2px 8px;border-radius:20px;font-size:.72rem;font-weight:700}
.chip-ok{background:#22c55e18;color:var(--ok);border:1px solid #22c55e33}
.chip-err{background:#ef444418;color:var(--err);border:1px solid #ef444433}
.sw-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:10px;margin-top:4px}
.sw-card{background:var(--s2);border:1px solid var(--border);border-radius:var(--r);padding:12px;text-align:center}
.sw-name{font-size:.72rem;color:var(--muted);margin-bottom:7px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sw-dot{width:44px;height:44px;border-radius:50%;margin:0 auto 7px;display:flex;align-items:center;justify-content:center;font-size:1.2rem}
.sw-dot.on{background:#22c55e18;border:2px solid var(--ok)}
.sw-dot.off{background:#ef444418;border:2px solid var(--err)}
.sw-dot.dis{background:var(--faint);border:2px solid var(--border)}
.sw-lbl{font-size:.75rem;font-weight:700}
.sw-lbl.on{color:var(--ok)}.sw-lbl.off{color:var(--err)}.sw-lbl.dis{color:var(--muted)}
.sw-gpio{font-size:.68rem;color:var(--faint);margin-top:3px}
.sw-sect{background:var(--s2);border:1px solid var(--border);border-radius:var(--r);padding:13px;margin-bottom:9px}
.sw-sect-hdr{font-size:.83rem;font-weight:600;display:flex;align-items:center;gap:7px;margin-bottom:10px}
.badge{background:var(--accent);color:#000;border-radius:20px;padding:1px 7px;font-size:.7rem;font-weight:700}
.net-item{display:flex;justify-content:space-between;align-items:center;
          padding:8px 11px;background:var(--s2);border:1px solid var(--border);
          border-radius:7px;margin-bottom:5px;cursor:pointer;font-size:.83rem;transition:border-color .15s}
.net-item:hover{border-color:var(--accent)}
@media(max-width:480px){.row{grid-template-columns:1fr}.btns{flex-direction:column}}
</style>
</head>
<body>
<header>
  <svg class="logo" viewBox="0 0 38 38" fill="none">
    <rect width="38" height="38" rx="9" fill="#00b4cc1a"/>
    <rect x="7" y="26" width="24" height="4" rx="2" fill="#00b4cc"/>
    <rect x="7" y="20" width="24" height="4" rx="2" fill="#00b4cc88"/>
    <rect x="7" y="14" width="24" height="4" rx="2" fill="#00b4cc44"/>
    <rect x="7" y="8"  width="24" height="4" rx="2" fill="#00b4cc22"/>
    <circle cx="19" cy="28" r="2" fill="#fff"/>
  </svg>
  <h1>PoolLevel <small id="mode-badge"></small></h1>
</header>
<div class="tabs">
  <button class="tab active" onclick="tab('status')">📊 Status</button>
  <button class="tab"        onclick="tab('wifi')">📶 WiFi</button>
  <button class="tab"        onclick="tab('mqtt')">📡 MQTT</button>
  <button class="tab"        onclick="tab('switches')">🔌 Switches</button>
</div>
<!-- STATUS -->
<div class="pane active" id="p-status">
  <div id="info-rows"></div>
  <div class="card">
    <div class="card-title">🌊 Switch States</div>
    <div class="sw-grid" id="sw-grid">Loading…</div>
  </div>
</div>
<!-- WIFI -->
<div class="pane" id="p-wifi">
  <div class="card">
    <div class="card-title">📶 WiFi</div>
    <label>SSID</label><input id="w-ssid" type="text" placeholder="Network name">
    <label>Password</label><input id="w-pass" type="password" placeholder="WiFi password">
    <div class="btns" style="margin-top:12px">
      <button class="btn btn-p" onclick="saveWifi()">💾 Save</button>
      <button class="btn btn-s" onclick="scanWifi()">🔍 Scan</button>
    </div>
    <div id="nets" style="margin-top:10px"></div>
    <div class="msg" id="wifi-msg"></div>
  </div>
</div>
<!-- MQTT -->
<div class="pane" id="p-mqtt">
  <div class="card">
    <div class="card-title">🏷 Device</div>
    <label>Device Name</label><input id="m-dname" type="text" placeholder="PoolLevel">
    <label>MQTT Client ID</label><input id="m-cid" type="text" placeholder="poollevel">
  </div>
  <div class="card">
    <div class="card-title">📡 Broker</div>
    <label>Host / IP</label><input id="m-host" type="text" placeholder="192.168.1.100">
    <div class="row">
      <div><label>Port</label><input id="m-port" type="number" placeholder="1883"></div>
      <div><label>Base Topic</label><input id="m-topic" type="text" placeholder="pool/level"></div>
    </div>
    <label>Username (optional)</label><input id="m-user" type="text">
    <label>Password (optional)</label><input id="m-pass" type="password">
    <div class="chk"><input id="m-ha" type="checkbox"><label for="m-ha">Home Assistant Auto-Discovery</label></div>
    <button class="btn btn-p" onclick="saveMqtt()">💾 Save</button>
    <div class="msg" id="mqtt-msg"></div>
  </div>
</div>
<!-- SWITCHES -->
<div class="pane" id="p-switches">
  <div class="card">
    <div class="card-title">🔌 GPIO / Switch Mapping</div>
    <div id="sw-forms"></div>
    <button class="btn btn-p" onclick="saveSwitches()">💾 Save &amp; Reboot</button>
    <div class="msg" id="sw-msg"></div>
  </div>
  <div class="card">
    <div class="card-title" style="color:var(--err)">⚠️ Danger Zone</div>
    <p style="font-size:.8rem;color:var(--muted);margin-bottom:10px">Erase all settings and reboot into setup AP.</p>
    <button class="btn btn-d" onclick="doReset()">🗑 Factory Reset</button>
  </div>
</div>
<script>
const GPIOS=[{v:0,l:'GPIO0/D3'},{v:2,l:'GPIO2/D4'},{v:4,l:'GPIO4/D2'},
             {v:5,l:'GPIO5/D1'},{v:12,l:'GPIO12/D6'},{v:13,l:'GPIO13/D7'},
             {v:14,l:'GPIO14/D5'},{v:16,l:'GPIO16/D0'}];
let cfg={};
function tab(n){
  ['status','wifi','mqtt','switches'].forEach((x,i)=>{
    document.querySelectorAll('.tab')[i].classList.toggle('active',x===n);
    document.getElementById('p-'+x).classList.toggle('active',x===n);
  });
  if(n==='status')refreshStatus();
}
async function api(p,m='GET',b=null){
  const o={method:m,headers:{'Content-Type':'application/json'}};
  if(b)o.body=JSON.stringify(b);
  return(await fetch(p,o)).json();
}
function msg(id,ok,t){
  const e=document.getElementById(id);
  e.className='msg '+(ok?'ok':'err');e.textContent=t;e.style.display='block';
  setTimeout(()=>e.style.display='none',3500);
}
async function load(){
  cfg=await api('/api/config');
  document.getElementById('w-ssid').value=cfg.wifi_ssid||'';
  document.getElementById('w-pass').value=cfg.wifi_pass||'';
  document.getElementById('m-dname').value=cfg.device_name||'PoolLevel';
  document.getElementById('m-cid').value=cfg.client_id||'poollevel';
  document.getElementById('m-host').value=cfg.mqtt_host||'';
  document.getElementById('m-port').value=cfg.mqtt_port||1883;
  document.getElementById('m-topic').value=cfg.mqtt_topic||'pool/level';
  document.getElementById('m-user').value=cfg.mqtt_user||'';
  document.getElementById('m-pass').value=cfg.mqtt_pass||'';
  document.getElementById('m-ha').checked=cfg.ha_discovery!==false;
  buildSwForms();refreshStatus();
}
function gpioSel(id,val){
  return'<select id="'+id+'">'+GPIOS.map(g=>'<option value="'+g.v+'"'+(g.v==val?' selected':'')+'>'+g.l+'</option>').join('')+'</select>';
}
function buildSwForms(){
  let h='';
  for(let i=0;i<4;i++){
    const s=cfg['sw'+i]||{};
    h+=`<div class="sw-sect"><div class="sw-sect-hdr"><span class="badge">${i+1}</span> Switch ${i+1}
      <label style="display:flex;align-items:center;gap:5px;margin:0 0 0 auto;font-size:.78rem;color:var(--text)">
        <input type="checkbox" id="s${i}en" ${s.enabled?'checked':''}> Enabled</label></div>
      <label>Label</label><input type="text" id="s${i}name" value="${s.name||'Switch_'+(i+1)}">
      <div class="row" style="margin-top:8px">
        <div><label>GPIO</label>${gpioSel('s'+i+'gpio',s.gpio||[4,5,12,14][i])}</div>
        <div><label>Logic</label><select id="s${i}al">
          <option value="1" ${s.actlow!==false?'selected':''}>Active LOW (pull-up)</option>
          <option value="0" ${s.actlow===false?'selected':''}>Active HIGH</option>
        </select></div>
      </div></div>`;
  }
  document.getElementById('sw-forms').innerHTML=h;
}
async function refreshStatus(){
  const r=await api('/api/status');
  document.getElementById('mode-badge').textContent=r.ap_mode?'📡 AP Mode':'';
  document.getElementById('info-rows').innerHTML=`
    <div class="info-row"><span class="lbl">WiFi</span><div>${r.wifi_ssid||'—'}
      <span class="chip ${r.wifi_connected?'chip-ok':'chip-err'}" style="margin-left:7px">${r.wifi_connected?'Connected':'Offline'}</span></div></div>
    <div class="info-row"><span class="lbl">IP</span><strong>${r.ip||'—'}</strong></div>
    <div class="info-row"><span class="lbl">MQTT</span><div>${r.mqtt_host||'—'}
      <span class="chip ${r.mqtt_connected?'chip-ok':'chip-err'}" style="margin-left:7px">${r.mqtt_connected?'Connected':'Offline'}</span></div></div>`;
  document.getElementById('sw-grid').innerHTML=r.switches.map((s)=>{
    const c=!s.enabled?'dis':s.state?'on':'off';
    const ic=!s.enabled?'⛔':s.state?'💧':'🔴';
    const lb=!s.enabled?'Disabled':s.state?'WET / ON':'DRY / OFF';
    return`<div class="sw-card"><div class="sw-name" title="${s.name}">${s.name}</div>
      <div class="sw-dot ${c}">${ic}</div><div class="sw-lbl ${c}">${lb}</div>
      <div class="sw-gpio">GPIO ${s.gpio}</div></div>`;
  }).join('');
}
async function saveWifi(){
  const r=await api('/api/save/wifi','POST',{wifi_ssid:document.getElementById('w-ssid').value,wifi_pass:document.getElementById('w-pass').value});
  msg('wifi-msg',r.ok,r.ok?'✅ Saved – reconnecting…':'❌ '+r.error);
}
async function scanWifi(){
  document.getElementById('nets').innerHTML='<div style="font-size:.8rem;color:var(--muted);padding:5px">Scanning…</div>';
  const r=await api('/api/scan');
  document.getElementById('nets').innerHTML=(r.networks||[]).map(n=>
    `<div class="net-item" onclick="document.getElementById('w-ssid').value='${n.ssid}'">
      <span>📶 ${n.ssid}</span><span style="color:var(--muted);font-size:.75rem">${n.rssi} dBm ${n.enc?'🔒':''}</span></div>`
  ).join('')||'<div style="font-size:.8rem;color:var(--muted)">No networks found.</div>';
}
async function saveMqtt(){
  const r=await api('/api/save/mqtt','POST',{
    device_name:document.getElementById('m-dname').value,
    client_id:document.getElementById('m-cid').value,
    mqtt_host:document.getElementById('m-host').value,
    mqtt_port:parseInt(document.getElementById('m-port').value)||1883,
    mqtt_topic:document.getElementById('m-topic').value,
    mqtt_user:document.getElementById('m-user').value,
    mqtt_pass:document.getElementById('m-pass').value,
    ha_discovery:document.getElementById('m-ha').checked
  });
  msg('mqtt-msg',r.ok,r.ok?'✅ Saved.':'❌ '+r.error);
}
async function saveSwitches(){
  const d={};
  for(let i=0;i<4;i++)d['sw'+i]={
    name:document.getElementById('s'+i+'name').value,
    gpio:parseInt(document.getElementById('s'+i+'gpio').value),
    actlow:document.getElementById('s'+i+'al').value==='1',
    enabled:document.getElementById('s'+i+'en').checked
  };
  const r=await api('/api/save/switches','POST',d);
  msg('sw-msg',r.ok,r.ok?'✅ Saved – rebooting…':'❌ '+r.error);
}
async function doReset(){
  if(!confirm('Erase all settings?'))return;
  await api('/api/reset','POST');
  alert('Reset done. Connect to WiFi "PoolLevel-Setup" (pw: poolsetup)');
}
setInterval(()=>{if(document.getElementById('p-status').classList.contains('active'))refreshStatus();},3000);
load();
</script>
</body>
</html>
)HTML";

void webui_setup(ESP8266WebServer &server, AppConfig &cfg) {
    (void)cfg;
    server.on("/", HTTP_GET, [&server](){
        server.sendHeader("Cache-Control","no-cache");
        server.send_P(200,"text/html",UI_HTML);
    });
}