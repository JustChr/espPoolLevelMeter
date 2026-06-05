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
  --accent:#00b4cc;--ok:#22c55e;--warn:#f59e0b;--err:#ef4444;--r:10px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
     font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;padding:16px}
header{display:flex;align-items:center;gap:12px;margin-bottom:20px;
       padding-bottom:14px;border-bottom:1px solid var(--border)}
.logo{width:38px;height:38px}
h1{font-size:1.2rem;font-weight:700}
h1 small{display:block;color:var(--muted);font-size:.76rem;font-weight:400;margin-top:2px}
.tabs{display:flex;gap:3px;background:var(--surface);border:1px solid var(--border);
      border-radius:var(--r);padding:4px;margin-bottom:18px;flex-wrap:wrap}
.tab{flex:1;min-width:60px;padding:7px 2px;border:none;background:transparent;
     color:var(--muted);cursor:pointer;border-radius:8px;
     font-size:.78rem;font-weight:500;transition:all .15s;white-space:nowrap}
.tab.active{background:var(--s2);color:var(--text)}
.tab:hover:not(.active){color:var(--text)}
.pane{display:none}.pane.active{display:block}
.card{background:var(--surface);border:1px solid var(--border);
      border-radius:var(--r);padding:16px;margin-bottom:14px}
.card-title{font-size:.88rem;font-weight:600;color:var(--accent);
            margin-bottom:12px;display:flex;align-items:center;gap:7px}
label{display:block;font-size:.78rem;color:var(--muted);
      margin-top:10px;margin-bottom:4px;font-weight:500}
label:first-of-type{margin-top:0}
input,select{width:100%;padding:8px 11px;background:var(--s2);
             border:1px solid var(--border);border-radius:7px;
             color:var(--text);font-size:.88rem;outline:none;
             transition:border-color .15s}
input:focus,select:focus{border-color:var(--accent)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.chk{display:flex;align-items:center;gap:7px;margin-top:10px}
.chk input{width:auto}.chk label{margin:0;color:var(--text)}
.btn{display:flex;align-items:center;justify-content:center;gap:6px;
     padding:9px 18px;border:none;border-radius:7px;font-size:.88rem;
     font-weight:600;cursor:pointer;transition:all .15s;width:100%;margin-top:10px}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn-p{background:var(--accent);color:#000}.btn-p:hover:not(:disabled){filter:brightness(1.1)}
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
.sw-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));
         gap:10px;margin-top:4px}
.sw-card{background:var(--s2);border:1px solid var(--border);
         border-radius:var(--r);padding:12px;text-align:center}
.sw-name{font-size:.72rem;color:var(--muted);margin-bottom:7px;
         white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sw-dot{width:44px;height:44px;border-radius:50%;margin:0 auto 7px;
        display:flex;align-items:center;justify-content:center;font-size:1.2rem}
.sw-dot.on{background:#22c55e18;border:2px solid var(--ok)}
.sw-dot.off{background:#ef444418;border:2px solid var(--err)}
.sw-dot.dis{background:var(--faint);border:2px solid var(--border)}
.sw-lbl{font-size:.75rem;font-weight:700}
.sw-lbl.on{color:var(--ok)}.sw-lbl.off{color:var(--err)}.sw-lbl.dis{color:var(--muted)}
.sw-gpio{font-size:.68rem;color:var(--faint);margin-top:3px}
.sw-sect{background:var(--s2);border:1px solid var(--border);
         border-radius:var(--r);padding:13px;margin-bottom:9px}
.sw-sect-hdr{font-size:.83rem;font-weight:600;
             display:flex;align-items:center;gap:7px;margin-bottom:10px}
.badge{background:var(--accent);color:#000;
       border-radius:20px;padding:1px 7px;font-size:.7rem;font-weight:700}
.net-item{display:flex;justify-content:space-between;align-items:center;
          padding:8px 11px;background:var(--s2);border:1px solid var(--border);
          border-radius:7px;margin-bottom:5px;cursor:pointer;
          font-size:.83rem;transition:border-color .15s}
.net-item:hover{border-color:var(--accent)}
code{background:var(--s2);padding:1px 6px;border-radius:4px;font-size:.85em}
#ota-drop{border:2px dashed var(--border);border-radius:var(--r);
          padding:28px;text-align:center;cursor:pointer;
          transition:border-color .15s;margin-bottom:12px}
#ota-drop:hover{border-color:var(--accent)}
.progress-wrap{display:none;margin-top:12px}
.progress-track{background:var(--s2);border-radius:20px;height:8px;overflow:hidden}
.progress-bar{height:100%;width:0%;background:var(--accent);transition:width .3s}
.progress-pct{font-size:.8rem;color:var(--muted);text-align:center;margin-top:6px}
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
  <button class="tab active" onclick="showTab('status')">&#x1F4CA; Status</button>
  <button class="tab"        onclick="showTab('wifi')">&#x1F4F6; WiFi</button>
  <button class="tab"        onclick="showTab('mqtt')">&#x1F4E1; MQTT</button>
  <button class="tab"        onclick="showTab('switches')">&#x1F50C; Switches</button>
  <button class="tab"        onclick="showTab('ota')">&#x2B06;&#xFE0F; OTA</button>
</div>

<!-- STATUS -->
<div class="pane active" id="p-status">
  <div id="info-rows"></div>
  <div class="card">
    <div class="card-title">&#x1F30A; Switch States</div>
    <div class="sw-grid" id="sw-grid">Loading&hellip;</div>
  </div>
  <div style="text-align:right;font-size:.72rem;color:var(--faint);margin-top:4px">
    Auto-refresh every 3 s &bull; FW <span id="fw-ver">&#x2014;</span>
  </div>
</div>

<!-- WIFI -->
<div class="pane" id="p-wifi">
  <div class="card">
    <div class="card-title">&#x1F4F6; WiFi Credentials</div>
    <label>SSID</label>
    <input id="w-ssid" type="text" placeholder="Network name" autocomplete="off">
    <label>Password</label>
    <input id="w-pass" type="password" placeholder="WiFi password" autocomplete="off">
    <div class="btns" style="margin-top:12px">
      <button class="btn btn-p" onclick="saveWifi()">&#x1F4BE; Save &amp; Reboot</button>
      <button class="btn btn-s" onclick="scanWifi()">&#x1F50D; Scan</button>
    </div>
    <div id="net-list" style="margin-top:10px"></div>
    <div class="msg" id="wifi-msg"></div>
  </div>
</div>

<!-- MQTT -->
<div class="pane" id="p-mqtt">
  <div class="card">
    <div class="card-title">&#x1F3F7;&#xFE0F; Device Identity</div>
    <label>Device Name <span style="color:var(--faint);font-weight:400">(friendly name in HA)</span></label>
    <input id="m-dname" type="text" placeholder="PoolLevel">
    <label>MQTT Client ID
      <span style="color:var(--faint);font-weight:400">&#x2014; also mDNS hostname:
        <code id="mdns-hint">poollevel.local</code>
      </span>
    </label>
    <input id="m-cid" type="text" placeholder="poollevel" oninput="updateMdnsHint()">
  </div>
  <div class="card">
    <div class="card-title">&#x1F4E1; MQTT Broker</div>
    <label>Host / IP</label>
    <input id="m-host" type="text" placeholder="192.168.1.100 or homeassistant.local">
    <div class="row">
      <div><label>Port</label>
           <input id="m-port" type="number" placeholder="1883" min="1" max="65535"></div>
      <div><label>Base Topic</label>
           <input id="m-topic" type="text" placeholder="pool/level"></div>
    </div>
    <label>Username <span style="color:var(--faint);font-weight:400">(optional)</span></label>
    <input id="m-user" type="text" autocomplete="off">
    <label>Password <span style="color:var(--faint);font-weight:400">(optional)</span></label>
    <input id="m-pass" type="password" autocomplete="off">
    <div class="chk">
      <input id="m-ha" type="checkbox">
      <label for="m-ha">Home Assistant MQTT Auto-Discovery</label>
    </div>
    <button class="btn btn-p" onclick="saveMqtt()">&#x1F4BE; Save &amp; Reboot</button>
    <div class="msg" id="mqtt-msg"></div>
  </div>
</div>

<!-- SWITCHES -->
<div class="pane" id="p-switches">
  <div class="card">
    <div class="card-title">&#x1F50C; GPIO / Switch Mapping</div>
    <div id="sw-forms"></div>
    <button class="btn btn-p" onclick="saveSwitches()">&#x1F4BE; Save &amp; Reboot</button>
    <div class="msg" id="sw-msg"></div>
  </div>
  <div class="card">
    <div class="card-title" style="color:var(--err)">&#x26A0;&#xFE0F; Danger Zone</div>
    <p style="font-size:.8rem;color:var(--muted);margin-bottom:10px">
      Erase all settings and reboot into setup AP mode.
    </p>
    <button class="btn btn-d" onclick="doReset()">&#x1F5D1; Factory Reset</button>
  </div>
</div>

<!-- OTA -->
<div class="pane" id="p-ota">
  <div class="card">
    <div class="card-title">&#x2B06;&#xFE0F; HTTP Firmware Update</div>
    <p style="font-size:.82rem;color:var(--muted);margin-bottom:14px;line-height:1.6">
      Upload a <code>.bin</code> built by PlatformIO
      (<em>Build</em> output: <code>.pio/build/d1_mini/firmware.bin</code>).<br>
      <strong style="color:var(--ok)">&#x2705; LittleFS partition is untouched &#x2014; settings survive.</strong><br>
      <strong style="color:var(--err)">&#x26A0;&#xFE0F; Do NOT upload a filesystem image here.</strong>
    </p>
    <div id="ota-drop" role="button" tabindex="0"
         onclick="document.getElementById('ota-file').click()"
         ondragover="event.preventDefault();this.style.borderColor='var(--accent)'"
         ondragleave="this.style.borderColor=''"
         ondrop="otaDrop(event)">
      <div style="font-size:2rem;margin-bottom:8px">&#x1F4E6;</div>
      <div id="ota-drop-lbl" style="font-size:.85rem;color:var(--muted)">
        Click or drag &amp; drop <code>.bin</code> here
      </div>
    </div>
    <input type="file" id="ota-file" accept=".bin" style="display:none"
           onchange="otaFileChosen(this)">
    <button class="btn btn-p" id="ota-btn" onclick="doOta()" disabled>
      &#x2B06;&#xFE0F; Upload Firmware
    </button>
    <div class="progress-wrap" id="ota-progress">
      <div class="progress-track"><div class="progress-bar" id="ota-bar"></div></div>
      <div class="progress-pct" id="ota-pct">0%</div>
    </div>
    <div class="msg" id="ota-msg"></div>
  </div>
  <div class="card">
    <div class="card-title">&#x1F4BB; PlatformIO / ArduinoOTA Push</div>
    <p style="font-size:.82rem;color:var(--muted);margin-bottom:12px;line-height:1.6">
      Select <code>d1_mini_ota</code> env in PlatformIO and click <strong>Upload</strong>.
      No IP needed &#x2014; uses mDNS hostname.
    </p>
    <div class="info-row">
      <span class="lbl">mDNS hostname</span>
      <strong><a id="ota-mdns-link" href="#"
                 style="color:var(--accent);text-decoration:none">&#x2014;</a></strong>
    </div>
    <div class="info-row"><span class="lbl">IP address</span><strong id="ota-ip">&#x2014;</strong></div>
    <div class="info-row"><span class="lbl">OTA port</span><strong>8266</strong></div>
    <div class="info-row"><span class="lbl">OTA password</span><code>ota-password</code></div>
  </div>
</div>

<script>
const TABS=['status','wifi','mqtt','switches','ota'];
const GPIOS=[
  {v:0,l:'GPIO0 / D3'},{v:2,l:'GPIO2 / D4'},
  {v:4,l:'GPIO4 / D2'},{v:5,l:'GPIO5 / D1'},
  {v:12,l:'GPIO12 / D6'},{v:13,l:'GPIO13 / D7'},
  {v:14,l:'GPIO14 / D5'},{v:16,l:'GPIO16 / D0'}
];
let cfg={};

function showTab(n){
  TABS.forEach((x,i)=>{
    document.querySelectorAll('.tab')[i].classList.toggle('active',x===n);
    document.getElementById('p-'+x).classList.toggle('active',x===n);
  });
  if(n==='ota') refreshOta();
}

async function api(path,method,body){
  const o={method:method||'GET',headers:{'Content-Type':'application/json'}};
  if(body!==undefined) o.body=JSON.stringify(body);
  return (await fetch(path,o)).json();
}

function showMsg(id,ok,text){
  const el=document.getElementById(id);
  el.className='msg '+(ok?'ok':'err');
  el.textContent=text; el.style.display='block';
  setTimeout(()=>{el.style.display='none';},5000);
}

async function load(){
  cfg=await api('/api/config');
  document.getElementById('w-ssid').value  =cfg.wifi_ssid  ||'';
  document.getElementById('w-pass').value  =cfg.wifi_pass  ||'';
  document.getElementById('m-dname').value =cfg.device_name||'PoolLevel';
  document.getElementById('m-cid').value   =cfg.client_id  ||'poollevel';
  document.getElementById('m-host').value  =cfg.mqtt_host  ||'';
  document.getElementById('m-port').value  =cfg.mqtt_port  ||1883;
  document.getElementById('m-topic').value =cfg.mqtt_topic ||'pool/level';
  document.getElementById('m-user').value  =cfg.mqtt_user  ||'';
  document.getElementById('m-pass').value  =cfg.mqtt_pass  ||'';
  document.getElementById('m-ha').checked  =cfg.ha_discovery!==false;
  updateMdnsHint();
  buildSwForms();
  refreshStatus();
}

function updateMdnsHint(){
  const cid=document.getElementById('m-cid').value||'poollevel';
  document.getElementById('mdns-hint').textContent=cid+'.local';
}

function gpioSelect(id,val){
  return '<select id="'+id+'">'+
    GPIOS.map(g=>'<option value="'+g.v+'"'+(g.v==val?' selected':'')+'>'+g.l+'</option>').join('')+
    '</select>';
}

function buildSwForms(){
  let h='';
  for(let i=0;i<4;i++){
    const s=cfg['sw'+i]||{};
    h+='<div class="sw-sect">'+
      '<div class="sw-sect-hdr">'+
        '<span class="badge">'+(i+1)+'</span> Switch '+(i+1)+
        '<label style="display:flex;align-items:center;gap:5px;margin:0 0 0 auto;font-size:.78rem;color:var(--text)">'+
          '<input type="checkbox" id="s'+i+'en"'+(s.enabled?' checked':'')+'> Enabled</label>'+
      '</div>'+
      '<label>Label</label>'+
      '<input type="text" id="s'+i+'name" value="'+(s.name||('Switch_'+(i+1)))+'">'+
      '<div class="row" style="margin-top:8px">'+
        '<div><label>GPIO</label>'+gpioSelect('s'+i+'gpio',s.gpio||[4,5,12,14][i])+'</div>'+
        '<div><label>Logic</label>'+
          '<select id="s'+i+'al">'+
            '<option value="1"'+(s.actlow!==false?' selected':'')+'>Active LOW (pull-up)</option>'+
            '<option value="0"'+(s.actlow===false?' selected':'')+'>Active HIGH</option>'+
          '</select>'+
        '</div>'+
      '</div>'+
    '</div>';
  }
  document.getElementById('sw-forms').innerHTML=h;
}

function infoRow(label,value,connected){
  let chip='';
  if(connected!==undefined)
    chip=' <span class="chip '+(connected?'chip-ok':'chip-err')+'">'+(connected?'Online':'Offline')+'</span>';
  return '<div class="info-row"><span class="lbl">'+label+'</span><div>'+value+chip+'</div></div>';
}

async function refreshStatus(){
  let r; try{r=await api('/api/status');}catch(e){return;}
  document.getElementById('mode-badge').textContent=r.ap_mode?'📡 AP Mode':'';
  document.getElementById('fw-ver').textContent=r.fw_version||'—';
  const mdns=r.mdns_hostname||'';
  document.getElementById('info-rows').innerHTML=
    infoRow('WiFi',r.wifi_ssid||'—',r.wifi_connected)+
    infoRow('IP','<strong>'+(r.ip||'—')+'</strong>')+
    infoRow('mDNS',mdns?'<a href="http://'+mdns+'" style="color:var(--accent);text-decoration:none">'+mdns+'</a>':'—')+
    infoRow('MQTT',(r.mqtt_host||'—'),r.mqtt_connected);
  document.getElementById('sw-grid').innerHTML=r.switches.map(s=>{
    const c=!s.enabled?'dis':s.state?'on':'off';
    const icon=!s.enabled?'&#x26D4;':s.state?'&#x1F4A7;':'&#x1F534;';
    const lbl=!s.enabled?'Disabled':s.state?'WET / ON':'DRY / OFF';
    return '<div class="sw-card">'+
      '<div class="sw-name" title="'+s.name+'">'+s.name+'</div>'+
      '<div class="sw-dot '+c+'">'+icon+'</div>'+
      '<div class="sw-lbl '+c+'">'+lbl+'</div>'+
      '<div class="sw-gpio">GPIO '+s.gpio+'</div>'+
    '</div>';
  }).join('');
}

async function refreshOta(){
  let r; try{r=await api('/api/status');}catch(e){return;}
  const mdns=r.mdns_hostname||'';
  const el=document.getElementById('ota-mdns-link');
  el.textContent=mdns||'—'; if(mdns) el.href='http://'+mdns;
  document.getElementById('ota-ip').textContent=r.ip||'—';
}

async function saveWifi(){
  const r=await api('/api/save/wifi','POST',{
    wifi_ssid:document.getElementById('w-ssid').value,
    wifi_pass:document.getElementById('w-pass').value});
  showMsg('wifi-msg',r.ok,r.ok?'✅ Saved — rebooting…':'❌ '+r.error);
}

async function scanWifi(){
  document.getElementById('net-list').innerHTML=
    '<div style="font-size:.8rem;color:var(--muted);padding:5px">Scanning…</div>';
  const r=await api('/api/scan');
  const nets=r.networks||[];
  document.getElementById('net-list').innerHTML=nets.length
    ?nets.map(n=>'<div class="net-item" onclick="document.getElementById(\'w-ssid\').value=\''+
        n.ssid.replace(/'/g,"\\'")+'\'">'+'<span>📶 '+n.ssid+'</span>'+
        '<span style="color:var(--muted);font-size:.75rem">'+n.rssi+' dBm '+(n.enc?'🔒':'')+'</span></div>').join('')
    :'<div style="font-size:.8rem;color:var(--muted)">No networks found.</div>';
}

async function saveMqtt(){
  const r=await api('/api/save/mqtt','POST',{
    device_name: document.getElementById('m-dname').value,
    client_id:   document.getElementById('m-cid').value,
    mqtt_host:   document.getElementById('m-host').value,
    mqtt_port:   parseInt(document.getElementById('m-port').value)||1883,
    mqtt_topic:  document.getElementById('m-topic').value,
    mqtt_user:   document.getElementById('m-user').value,
    mqtt_pass:   document.getElementById('m-pass').value,
    ha_discovery:document.getElementById('m-ha').checked});
  showMsg('mqtt-msg',r.ok,r.ok?'✅ Saved — rebooting to apply new hostname…':'❌ '+r.error);
}

async function saveSwitches(){
  const body={};
  for(let i=0;i<4;i++) body['sw'+i]={
    name:   document.getElementById('s'+i+'name').value,
    gpio:   parseInt(document.getElementById('s'+i+'gpio').value),
    actlow: document.getElementById('s'+i+'al').value==='1',
    enabled:document.getElementById('s'+i+'en').checked};
  const r=await api('/api/save/switches','POST',body);
  showMsg('sw-msg',r.ok,r.ok?'✅ Saved — rebooting…':'❌ '+r.error);
}

async function doReset(){
  if(!confirm('Erase ALL settings and reboot into AP setup mode?')) return;
  await api('/api/reset','POST');
  alert('Reset done.\nConnect to WiFi "PoolLevel-Setup" (password: poolsetup).');
}

function otaFileChosen(input){
  const f=input.files[0]; if(!f) return;
  document.getElementById('ota-drop-lbl').textContent=
    '📦 '+f.name+' ('+(f.size/1024).toFixed(1)+' KB)';
  document.getElementById('ota-drop').style.borderColor='var(--accent)';
  document.getElementById('ota-btn').disabled=false;
}
function otaDrop(e){
  e.preventDefault();
  document.getElementById('ota-drop').style.borderColor='';
  const dt=e.dataTransfer;
  if(dt&&dt.files.length){
    try{document.getElementById('ota-file').files=dt.files;}catch(_){}
    otaFileChosen({files:dt.files});
  }
}
function doOta(){
  const file=document.getElementById('ota-file').files[0]; if(!file) return;
  const fd=new FormData(); fd.append('firmware',file);
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  const prog=document.getElementById('ota-progress');
  const bar=document.getElementById('ota-bar');
  const pct=document.getElementById('ota-pct');
  prog.style.display='block';
  document.getElementById('ota-btn').disabled=true;
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      const p=Math.round(e.loaded*100/e.total);
      bar.style.width=p+'%'; pct.textContent=p+'%';
    }
  };
  xhr.onload=function(){
    const ok=xhr.status===200;
    showMsg('ota-msg',ok,ok?'✅ Update successful — device is rebooting…':'❌ Update failed: '+xhr.responseText);
  };
  xhr.onerror=function(){
    showMsg('ota-msg',false,'❌ Network error during upload.');
    document.getElementById('ota-btn').disabled=false;
  };
  xhr.send(fd);
}

setInterval(function(){
  if(document.getElementById('p-status').classList.contains('active')) refreshStatus();
},3000);

load();
</script>
</body>
</html>
)HTML";

void webui_setup(ESP8266WebServer &server, AppConfig &cfg) {
    (void)cfg;
    server.on("/", HTTP_GET, [&server]() {
        server.sendHeader("Cache-Control", "no-cache, no-store");
        server.send_P(200, "text/html", UI_HTML);
    });
}