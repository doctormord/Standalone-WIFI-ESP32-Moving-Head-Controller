#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ArtnetWifi.h> 
#include <ESPmDNS.h> 
#include <math.h> 
#include <Update.h> 

// =========================================================
// --- 1. HARDWARE CONFIGURATION (DMX CHANNELS & MAPS) ---
// =========================================================
#define NUM_CHANNELS  18
#define CH_DIMMER     1
#define CH_STROBE     2
#define CH_PAN        3
#define CH_TILT       4
#define CH_SPEED      5
#define CH_COLOR      6
#define CH_GOBO       7
#define CH_GOBO_ROT   8
#define CH_GOBO_IDX   9
#define CH_PRISM      10
#define CH_PRISM_ROT  11
#define CH_FROST      12
#define CH_FOCUS      13
#define CH_ZOOM       14
#define CH_PAN_FINE   15
#define CH_TILT_FINE  16
#define CH_MACRO      17
#define CH_RESET      18

const byte wheelMap[20] = {0, 50, 5, 55, 10, 60, 15, 65, 20, 70, 25, 75, 30, 80, 35, 85, 40, 90, 45, 95};
const byte sGoboMap[10] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90}; 
const byte rGoboMap[7]  = {0, 10, 20, 30, 40, 50, 60};

// --- WIFI CONFIGURATION ---
const char* ap_ssid = "Moving_Head_Ctrl";   
const char* ap_password = "12345678";  
int transmitPin = 7; 

// =========================================================
// --- SYSTEM VARIABLES ---
// =========================================================
WebServer server(80);
ArtnetWifi artnet;
Preferences prefs;
byte dmxData[513]; 

String presetNames[10];

int globalBPM = 120;
unsigned long lastBeatTime = 0;
bool beatTriggered = false; 
bool manualTap = false; 
const float syncBeats[7] = {8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125};

bool bumpBlackout = false; bool bumpStrobeF = false; bool bumpStrobe50 = false; bool bumpBlinder = false;
int activePresetSlot = 0;

int centerPan16 = 32767; int centerTilt16 = 32767;

// --- SMART DIMMER & AUTO FADE ---
int dimSmoothVal = 0; 
float dimSmoothTarget = 0.0;
float dimSmoothCurrent = 0.0;

bool autoFading = false;
bool fadeStateOut = false; 
unsigned long fadeStartTime = 0;
unsigned long fadeDuration = 2000;
int fadeCurve = 3;
float fadeMultiplier = 1.0;

// --- FX ENGINES ---
bool fxActive = false; int fxType = 1; float fxRot = 0.0;   
int fxTrigger = 0; int fxSync = 3; 
int fxSpdSt = 50; int fxSpdEn = 50; int fxSzSt = 30; int fxSzEn = 30;
int fxModMo = 1; int fxModCu = 0; float fxModSp = 0.5;
float fxModPhase = 0.0; float fxTheta = 0.0; float lastFxRand = 0.0; float nextFxRand = 0.0;
unsigned long lastFxUpdate = 0;

bool dimFxActive = false; int dimFxStart = 0; int dimFxEnd = 255;
int dimFxMode = 1; int dimFxCurve = 0; float dimFxSpeed = 1.0; 
int dimFxTrigger = 0; int dimFxSync = 3; 
float dimFxPhase = 0.0; float lastDimRand = 0.0; float nextDimRand = 0.0;
unsigned long lastDimFxUpdate = 0;

bool gRotFxActive = false; int gRotFxStart = 135; int gRotFxEnd = 255;
int gRotFxMode = 1; int gRotFxCurve = 0; float gRotFxSpeed = 1.0; 
int gRotFxTrigger = 0; int gRotFxSync = 3; 
float gRotFxPhase = 0.0; float lastGRotRand = 0.0; float nextGRotRand = 0.0;
unsigned long lastGRotFxUpdate = 0;

bool pRotFxActive = false; int pRotFxStart = 135; int pRotFxEnd = 255;
int pRotFxMode = 1; int pRotFxCurve = 0; float pRotFxSpeed = 1.0; 
int pRotFxTrigger = 0; int pRotFxSync = 3; 
float pRotFxPhase = 0.0; float lastPRotRand = 0.0; float nextPRotRand = 0.0;
unsigned long lastPRotFxUpdate = 0;

bool colFxActive = false; int colFxStart = 0; int colFxEnd = 19; int colFxStep = 1;
int colFxTrigger = 0; int colFxSync = 3; unsigned long colFxHoldTime = 1000; unsigned long colFxStepStartTime = 0; int currentColIdx = 0;

bool sgobFxActive = false; int sgobFxStart = 0; int sgobFxEnd = 9;
int sgobFxTrigger = 0; int sgobFxSync = 3; unsigned long sgobFxHoldTime = 1000; unsigned long sgobFxStepStartTime = 0;
int currentSGobIdx = 0; bool sgobFxScratch = false;

bool rgobFxActive = false; int rgobFxStart = 0; int rgobFxEnd = 6;
int rgobFxTrigger = 0; int rgobFxSync = 3; unsigned long rgobFxHoldTime = 1000; unsigned long rgobFxStepStartTime = 0;
int currentRGobIdx = 0; bool rgobFxScratch = false;

// --- SCENE CHASER ---
struct SceneData {
  byte dmx[19];
  bool fA; int fT; float fR; int fTr; int fSy; int fSS; int fSE; int fZS; int fZE; int fMM; int fMC; float fMS;
  bool dA; int dSt; int dEn; int dMo; int dCu; float dSp; int dTr; int dSy;
  bool grA; int grSt; int grEn; int grMo; int grCu; float grSp; int grTr; int grSy;
  bool prA; int prSt; int prEn; int prMo; int prCu; float prSp; int prTr; int prSy;
  bool cA; int cSt; int cEn; unsigned long cHo; int cTr; int cSy;
  bool sgA; int sgSt; int sgEn; unsigned long sgHo; int sgTr; int sgSy; bool sgSc;
  bool rgA; int rgSt; int rgEn; unsigned long rgHo; int rgTr; int rgSy; bool rgSc;
};
SceneData chaserScenes[10]; 

bool chaserActive = false; int chaserStartSlot = 0; int chaserEndSlot = 3; 
int chaserTrigger = 0; int chaserSync = 3; int chaserOrder = 0;
unsigned long fadeTime = 2000; unsigned long holdTime = 2000; unsigned long stepStartTime = 0;
int currentSlot = 0; int nextSlot = 1; bool inFade = false;

// =========================================================
// --- HTML & JAVASCRIPT UI ---
// =========================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>PRO FIXTURE CONSOLE</title><meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<style>
body{font-family:sans-serif;text-align:center;background:#121212;color:#fff;padding:10px;margin:0;overscroll-behavior-y:contain;}
h2{color:#00d2ff;font-size:1.4em;margin-bottom:15px;letter-spacing:2px;text-transform:uppercase;}
.grid-wrapper{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:15px;align-items:start;text-align:left;}
.tab-bar{display:flex;gap:5px;margin-bottom:15px;border-bottom:2px solid #333;}
.tab-bar button{flex:1;padding:15px 5px;font-size:1em;background:#222;color:#888;border:none;border-radius:8px 8px 0 0;font-weight:bold;cursor:pointer;transition:0.2s;}
.tab-bar button.active{background:#1e1e1e;color:#00d2ff;border-bottom:3px solid #00d2ff;}
.tab-content{display:none;} .tab-content.active{display:block;}
.bump-btn{font-size:1.1em;padding:25px 5px;border-radius:10px;font-weight:bold;text-transform:uppercase;border:none;cursor:pointer;touch-action:manipulation;transition:0.1s;}
.bump-btn:active{transform:scale(0.95);opacity:0.8;}
.grid-5{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;margin-bottom:15px;}
.preset-btn{background:#222;border:2px solid #333;color:#555;padding:12px 5px;font-size:1.2em;border-radius:10px;font-weight:bold;transition:0.2s;cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;line-height:1.3;overflow:hidden;}
.preset-btn.active{background:#007bff;border-color:#00d2ff;color:#fff;box-shadow:0 0 15px rgba(0,210,255,0.4);text-shadow:0 0 5px #fff;}
.p-num{font-size:1.1em;}
.p-name{font-size:0.55em;font-weight:normal;color:#aaa;white-space:nowrap;text-overflow:ellipsis;overflow:hidden;width:100%;margin-top:4px;}
.preset-btn.active .p-name{color:#e0e0e0;text-shadow:none;}
.section{background:#1e1e1e;padding:15px;border-radius:10px;border:1px solid #333;}
h3{margin-top:0;font-size:0.85em;color:#aaa;text-align:left;border-bottom:1px solid #333;padding-bottom:5px;text-transform:uppercase;}
.slider-row{margin-bottom:15px;}
label{font-weight:bold;font-size:0.85em;display:flex;justify-content:space-between;margin-bottom:6px;}
.val{color:#00d2ff;}
input[type=range]{-webkit-appearance:none;width:100%;background:#333;height:12px;border-radius:6px;outline:none;}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:#00d2ff;}
select{width:100%;padding:12px;border-radius:6px;background:#333;color:white;border:1px solid #555;font-size:0.95em;}
optgroup{background:#222;color:#00d2ff;}
.btn-group{display:flex;gap:8px;margin-bottom:8px;}
button{flex:1;padding:12px;border:none;border-radius:6px;font-size:0.85em;font-weight:bold;cursor:pointer;background:#333;color:white;touch-action:manipulation;}
.btn-active{background:#00d2ff!important;color:#000!important;}
.btn-danger{background:#5a1010;color:white!important;}
.btn-toggle{width:100%;padding:15px;font-size:1.1em;margin-top:10px;transition:0.2s;}
.joy-container{width:200px;height:200px;background:#222;border-radius:50%;margin:15px auto;position:relative;border:2px solid #555;touch-action:none;background-image:radial-gradient(#444 1px,transparent 1px);background-size:20px 20px;}
.stick{width:45px;height:45px;background:#00d2ff;border-radius:50%;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);pointer-events:none;box-shadow:0 0 15px rgba(0,210,255,0.5);}
.compound-row{display:flex;flex-direction:column;gap:8px;margin-bottom:12px;align-items:stretch;} 
.compound-row select{width:100%;} .compound-row input{width:100%;display:none;margin-top:5px;}
details{background:#222;border-radius:8px;margin-bottom:15px;border:1px solid #444;text-align:left;}
summary{padding:12px;cursor:pointer;font-weight:bold;color:#00d2ff;text-align:center;}
input[type=text],input[type=password]{width:100%;padding:12px;background:#333;color:white;border:1px solid #555;border-radius:6px;box-sizing:border-box;}
</style>
<script>
  const CH_DIMMER=1, CH_STROBE=2, CH_PAN=3, CH_TILT=4, CH_SPEED=5, CH_COLOR=6;
  const CH_GOBO=7, CH_GOBO_ROT=8, CH_GOBO_IDX=9, CH_PRISM=10, CH_PRISM_ROT=11;
  const CH_FROST=12, CH_FOCUS=13, CH_ZOOM=14, CH_PAN_FINE=15, CH_TILT_FINE=16, CH_MACRO=17;

  const COLORS=[{n:"White",v:0},{n:"W/R",v:50},{n:"Red",v:5},{n:"R/Y",v:55},{n:"Yellow",v:10},{n:"Y/B",v:60},{n:"Blue",v:15},{n:"B/G",v:65},{n:"Green",v:20},{n:"G/P",v:70},{n:"Purple",v:25},{n:"P/A",v:75},{n:"Amber",v:30},{n:"A/C",v:80},{n:"Cyan",v:35},{n:"C/E",v:85},{n:"Emerald",v:40},{n:"E/O",v:90},{n:"Orange",v:45},{n:"O/W",v:95}];
  const SGOBOS=[{n:"White (Open)",v:0},{n:"Gobo 1",v:10},{n:"Gobo 2",v:20},{n:"Gobo 3",v:30},{n:"Gobo 4",v:40},{n:"Gobo 5",v:50},{n:"Gobo 6",v:60},{n:"Gobo 7",v:70},{n:"Gobo 8",v:80},{n:"Gobo 9",v:90}];
  const RGOBOS=[{n:"White (Open)",v:0},{n:"Gobo 1",v:10},{n:"Gobo 2",v:20},{n:"Gobo 3",v:30},{n:"Gobo 4",v:40},{n:"Gobo 5",v:50},{n:"Gobo 6",v:60}];

  const syncOpts = `<option value="0">Sync: 8 Beats</option><option value="1">Sync: 4 Beats</option><option value="2">Sync: 2 Beats</option><option value="3" selected>Sync: 1 Beat</option><option value="4">Sync: 1/2 Beat</option><option value="5">Sync: 1/4 Beat</option><option value="6">Sync: 1/8 Beat</option>`;

  let pendingUpdates={},sendTimer=null,dmxState={};
  let currentPan16=32767,currentTilt16=32767,joyPanRev=false,joyTiltRev=false;
  let presetNamesArr = Array(10).fill(''); // Speichert die Namen für die UI

  function formatRot(v) {
    if(v < 135) return '';
    if(v <= 180) return `[REV ${Math.round(((180-v)/45)*100)}%]`;
    if(v <= 190) return `[STOP]`;
    return `[FWD ${Math.round(((v-190)/65)*100)}%]`;
  }

  function syncJoySettings(src) {
    let spd = document.getElementById(src+'JoySpd').value; let crv = document.getElementById(src+'JoyCrv').value;
    ['l','f','p'].forEach(t => {
      if(t !== src) {
        if(document.getElementById(t+'JoySpd')) document.getElementById(t+'JoySpd').value = spd;
        if(document.getElementById(t+'JoyCrv')) document.getElementById(t+'JoyCrv').value = crv;
      }
      if(document.getElementById('v'+t+'JoySpd')) document.getElementById('v'+t+'JoySpd').innerText = spd;
      if(document.getElementById('v'+t+'JoyCrv')) document.getElementById('v'+t+'JoyCrv').innerText = crv;
    });
  }

  function initDropdowns() {
    document.querySelectorAll('.sync-list').forEach(s => s.innerHTML = syncOpts);
    let cOpt=''; COLORS.forEach((c,i)=>cOpt+=`<option value="${i}">${c.n}</option>`); document.querySelectorAll('.col-list').forEach(s=>s.innerHTML=cOpt);
    let sOpt=''; SGOBOS.forEach((g,i)=>sOpt+=`<option value="${i}">${g.n}</option>`); document.querySelectorAll('.sgob-list').forEach(s=>s.innerHTML=sOpt);
    let rOpt=''; RGOBOS.forEach((g,i)=>rOpt+=`<option value="${i}">${g.n}</option>`); document.querySelectorAll('.rgob-list').forEach(s=>s.innerHTML=rOpt);
    
    if(document.getElementById('colEn')) document.getElementById('colEn').value=19; 
    if(document.getElementById('sgobEn')) document.getElementById('sgobEn').value=9; 
    if(document.getElementById('rgobEn')) document.getElementById('rgobEn').value=6;
    
    if(document.getElementById('base6')) { let h='<optgroup label="Colors">'; COLORS.forEach(c=>h+=`<option value="${c.v},0">${c.n}</option>`); h+='</optgroup><optgroup label="Rot"><option value="100,155">Rot(Slow->Fast)</option></optgroup>'; document.getElementById('base6').innerHTML=h; }
    if(document.getElementById('base7')) { let h='<optgroup label="Gobos">'; SGOBOS.forEach(g=>h+=`<option value="${g.v},0">${g.n}</option>`); h+='</optgroup><optgroup label="Rot"><option value="100,29">Rev</option><option value="130,4">Stop</option><option value="135,75">Fwd</option><option value="211,44">Shake</option></optgroup>'; document.getElementById('base7').innerHTML=h; }
    if(document.getElementById('base8')) { let h='<optgroup label="Gobos">'; RGOBOS.forEach(g=>h+=`<option value="${g.v},0">${g.n}</option>`); h+='</optgroup><optgroup label="Rot"><option value="70,57">Fwd</option><option value="128,1">Stop</option><option value="130,62">Rev</option><option value="193,62">Shake</option></optgroup>'; document.getElementById('base8').innerHTML=h; }
  }

  function switchTab(id,btn) { document.querySelectorAll('.tab-content,.tab-bar button').forEach(e=>e.classList.remove('active')); document.getElementById(id).classList.add('active'); btn.classList.add('active'); }
  function sendBump(t,s) { fetch(`/bump?t=${t}&s=${s}`); }
  function killAllFx() { fetch('/kill_fx').then(()=>syncUI()); }
  function clearBeam() { killAllFx(); updateDMX(CH_COLOR,0); updateDMX(CH_GOBO,0); updateDMX(CH_GOBO_ROT,0); updateDMX(CH_GOBO_IDX,0); updateDMX(CH_PRISM,0); updateDMX(CH_PRISM_ROT,0); updateDMX(CH_FROST,0); }
  function updateDimSmooth(val) { document.getElementById('vDimSm').innerText = val; fetch('/smooth?v=' + val); }
  
  function triggerAutoFade() {
    let time = document.getElementById('afTime').value; let crv = document.getElementById('afCurve').value;
    fetch(`/autofade?t=${time}&c=${crv}`);
  }

  function updateSlotInput() {
    let sel = document.getElementById('slotSel');
    if(sel) document.getElementById('slotName').value = presetNamesArr[sel.value - 1] || "";
  }

  let tapTimes=[];
  function tapTempo() {
    let now=Date.now(); if(tapTimes.length>0 && now-tapTimes[tapTimes.length-1]>2000) tapTimes=[]; tapTimes.push(now); if(tapTimes.length>8) tapTimes.shift(); 
    if(tapTimes.length>1){ let d=[]; for(let i=1;i<tapTimes.length;i++) d.push(tapTimes[i]-tapTimes[i-1]); let avg=d.reduce((a,b)=>a+b)/d.length; let bpm=Math.round(60000/avg); document.querySelectorAll('.bpm-disp').forEach(e=>e.innerText=bpm); fetch(`/bpm?v=${bpm}`); } else { fetch('/beat'); }
  }
  function hardSync() { fetch('/sync'); }

  let audioCtx,analyser,dataArr,audioSync=false,lastBeat=0;
  async function toggleAudio() {
    if(!audioSync) {
      try {
        const stream=await navigator.mediaDevices.getUserMedia({audio:true}); audioCtx=new(window.AudioContext||window.webkitAudioContext)();
        const src=audioCtx.createMediaStreamSource(stream); analyser=audioCtx.createAnalyser(); analyser.fftSize=256; src.connect(analyser);
        dataArr=new Uint8Array(analyser.frequencyBinCount); audioSync=true; document.querySelectorAll('.btn-mic').forEach(b=>b.style.background="#e83e8c"); detectBeat();
      } catch(e){alert("Mic error/HTTPS needed");}
    } else { audioSync=false; audioCtx.close(); document.querySelectorAll('.btn-mic').forEach(b=>b.style.background="#28a745"); }
  }
  function detectBeat() {
    if(!audioSync) return; requestAnimationFrame(detectBeat); analyser.getByteFrequencyData(dataArr);
    if(((dataArr[1]+dataArr[2]+dataArr[3])/3)>210 && Date.now()-lastBeat>350) { lastBeat=Date.now(); fetch('/beat'); document.querySelectorAll('.btn-mic').forEach(b=>{b.style.background="#fff"; setTimeout(()=>b.style.background="#e83e8c",100);}); }
  }

  // Hintergrund-Polling (Updatet Live-Tabs & Dropdown-Texte)
  setInterval(async()=>{ 
    if(document.visibilityState!=='visible') return; 
    try{
      const r=await fetch('/api/state');const d=await r.json();
      document.querySelectorAll('.bpm-disp').forEach(e=>e.innerText=d.bpm);
      document.querySelectorAll('.preset-btn').forEach(b=>b.classList.remove('active'));
      if(d.pr>0){let pb=document.getElementById('pre'+d.pr);if(pb)pb.classList.add('active');} 
      if(d.pn){
        presetNamesArr = d.pn;
        d.pn.forEach((n,i)=>{
          let el=document.getElementById('pn'+(i+1)); if(el) el.innerText=n;
          let opt=document.querySelector(`#slotSel option[value="${i+1}"]`); if(opt) opt.innerText = (i+1)+": "+(n||"Empty");
        });
      } 
      setToggleBtn(d.chA===1,'btnChaToggleL','SHOW','#ffc107');setToggleBtn(d.chA===1,'btnChaToggle','SHOW','#ffc107');
      
      let fadeBtn = document.getElementById('btnAutoFade');
      if(fadeBtn) {
        if(d.fO === 1) { fadeBtn.innerText = "FADE IN (TO SLIDER)"; fadeBtn.style.background = "#28a745"; } 
        else { fadeBtn.innerText = "FADE OUT (TO BLACK)"; fadeBtn.style.background = "#5a1010"; }
      }
    }catch(e){} 
  }, 500);

  function toggleJoyRev(a) { if(a==='pan'){joyPanRev=!joyPanRev;document.querySelectorAll('.btn-pan-rev').forEach(b=>b.className='btn-pan-rev '+(joyPanRev?'btn-active':''));}else{joyTiltRev=!joyTiltRev;document.querySelectorAll('.btn-tilt-rev').forEach(b=>b.className='btn-tilt-rev '+(joyTiltRev?'btn-active':''));} }
  function resetCenter() { currentPan16=32767; currentTilt16=32767; updateDMX(CH_PAN,127); updateDMX(CH_PAN_FINE,255); updateDMX(CH_TILT,127); updateDMX(CH_TILT_FINE,255); document.querySelectorAll('.stick').forEach(s=>{s.style.left='50%';s.style.top='50%';}); }
  function toggleEffect(ch,selector) { let newVal=(dmxState[ch]||0)>127?0:255; updateDMX(ch,newVal); document.querySelectorAll(selector).forEach(b=>b.classList.toggle('btn-active',newVal>127)); }
  
  function updateDMX(ch,val) { 
    val=Math.round(val); if(dmxState[ch]===val) return; dmxState[ch]=val; 
    document.querySelectorAll(".v"+ch).forEach(e=>{ e.innerText = (ch===CH_GOBO_IDX || ch===CH_PRISM_ROT) && val>=135 ? val + " " + formatRot(val) : val; });
    document.querySelectorAll(".in"+ch).forEach(e=>e.value=val);
    pendingUpdates[ch]=val; if(!sendTimer) sendTimer=setTimeout(sendBatch,40); 
  }
  
  async function sendBatch() { sendTimer=null; const k=Object.keys(pendingUpdates); if(k.length===0) return; const p=new URLSearchParams(); k.forEach(x=>p.append("c"+x,pendingUpdates[x])); pendingUpdates={}; try{await fetch(`/set_all?${p.toString()}`);}catch(e){} }
  function updateCompound(ch,bc,id) { const sel=document.getElementById(id), sld=document.getElementById('off'+ch); if(!sel||!sld) return; const p=sel.value.split(','); const b=parseInt(p[0]), m=parseInt(p[1]); if(bc){sld.max=m;if(parseInt(sld.value)>m)sld.value=m;sld.style.display=m>0?'block':'none';} updateDMX(ch,Math.min(255,b+(m>0?parseInt(sld.value):0))); }
  function setToggleBtn(st,id,lbl,col) { const b=document.getElementById(id); if(!b)return; if(st){b.innerText="STOP "+lbl;b.style.background=col;b.style.color="#000";}else{b.innerText="START "+lbl;b.style.background="#28a745";b.style.color="#fff";} }

  async function syncUI() {
    const r=await fetch('/api/get_dmx'); const d=await r.json(); currentPan16=d.cp; currentTilt16=d.ct; document.querySelectorAll('.bpm-disp').forEach(e=>e.innerText=d.bpm);
    document.querySelectorAll('.preset-btn').forEach(b=>b.classList.remove('active')); if(d.pr>0){let p=document.getElementById('pre'+d.pr);if(p)p.classList.add('active');}
    
    if(d.pn){
      presetNamesArr = d.pn;
      d.pn.forEach((n,i)=>{
        let el=document.getElementById('pn'+(i+1)); if(el)el.innerText=n;
        let opt=document.querySelector(`#slotSel option[value="${i+1}"]`); if(opt) opt.innerText = (i+1)+": "+(n||"Empty");
      });
    }
    
    if(d.dSm!==undefined) { if(document.getElementById('dimSm')) document.getElementById('dimSm').value=d.dSm; if(document.getElementById('vDimSm')) document.getElementById('vDimSm').innerText=d.dSm; }
    
    for(let i=1;i<=18;i++){ 
      const v=d[i]; dmxState[i]=v; 
      document.querySelectorAll(".v"+i).forEach(e=>{ e.innerText = (i===CH_GOBO_IDX || i===CH_PRISM_ROT) && v>=135 ? v + " " + formatRot(v) : v; });
      document.querySelectorAll(".in"+i).forEach(e=>e.value=v);
      if(i===CH_PRISM) document.querySelectorAll('.btn-prism').forEach(b=>b.classList.toggle('btn-active',v>127)); 
      if(i===CH_FROST) document.querySelectorAll('.btn-frost').forEach(b=>b.classList.toggle('btn-active',v>127)); 
      if(i===CH_COLOR) { let cv=COLORS.find(x=>x.v===v); if(cv) document.querySelectorAll('.col-list').forEach(s=>s.value=COLORS.indexOf(cv)); }
      const bs=document.getElementById("base"+i); if(bs){ for(let opt of bs.options){ let p=opt.value.split(',');let b=parseInt(p[0]),rg=parseInt(p[1]); if(v>=b&&v<=b+rg){ bs.value=opt.value;const sld=document.getElementById("off"+i); if(sld){sld.max=rg;sld.value=v-b;sld.style.display=rg>0?'block':'none';}break; } } } 
    }

    if(d.fA!==undefined){
      if(document.getElementById('fxType')) document.getElementById('fxType').value=d.fT;
      if(document.getElementById('fxRot')) document.getElementById('fxRot').value=d.fR;
      if(document.getElementById('fxTr')) document.getElementById('fxTr').value=d.fTr;
      if(document.getElementById('fxSy')) document.getElementById('fxSy').value=d.fSy;
      if(document.getElementById('fxSS')) document.getElementById('fxSS').value=d.fSS;
      if(document.getElementById('fxSE')) document.getElementById('fxSE').value=d.fSE;
      if(document.getElementById('fxZS')) document.getElementById('fxZS').value=d.fZS;
      if(document.getElementById('fxZE')) document.getElementById('fxZE').value=d.fZE;
      if(document.getElementById('fxMM')) document.getElementById('fxMM').value=d.fMM;
      if(document.getElementById('fxMC')) document.getElementById('fxMC').value=d.fMC;
      if(document.getElementById('fxMS')) document.getElementById('fxMS').value=d.fMS;
      updateFxUI(false); setToggleBtn(d.fA,'btnFxToggle','MOVEMENT FX','#17a2b8');
      
      if(document.getElementById('dimSt')) document.getElementById('dimSt').value=d.dSt;
      if(document.getElementById('dimEn')) document.getElementById('dimEn').value=d.dEn;
      if(document.getElementById('dimMo')) document.getElementById('dimMo').value=d.dMo;
      if(document.getElementById('dimCu')) document.getElementById('dimCu').value=d.dCu;
      if(document.getElementById('dimSp')) document.getElementById('dimSp').value=d.dSp;
      if(document.getElementById('dimTr')) document.getElementById('dimTr').value=d.dTr;
      if(document.getElementById('dimSy')) document.getElementById('dimSy').value=d.dSy;
      updateModUI('dim',false); setToggleBtn(d.dA,'btnDimToggle','DIMMER FX','#e83e8c');

      if(document.getElementById('grSt')) document.getElementById('grSt').value=d.grSt;
      if(document.getElementById('grEn')) document.getElementById('grEn').value=d.grEn;
      if(document.getElementById('grMo')) document.getElementById('grMo').value=d.grMo;
      if(document.getElementById('grCu')) document.getElementById('grCu').value=d.grCu;
      if(document.getElementById('grSp')) document.getElementById('grSp').value=d.grSp;
      if(document.getElementById('grTr')) document.getElementById('grTr').value=d.grTr;
      if(document.getElementById('grSy')) document.getElementById('grSy').value=d.grSy;
      updateModUI('gr',false); setToggleBtn(d.grA,'btnGRotToggle','GOBO ROT FX','#6f42c1');

      if(document.getElementById('prSt')) document.getElementById('prSt').value=d.prSt;
      if(document.getElementById('prEn')) document.getElementById('prEn').value=d.prEn;
      if(document.getElementById('prMo')) document.getElementById('prMo').value=d.prMo;
      if(document.getElementById('prCu')) document.getElementById('prCu').value=d.prCu;
      if(document.getElementById('prSp')) document.getElementById('prSp').value=d.prSp;
      if(document.getElementById('prTr')) document.getElementById('prTr').value=d.prTr;
      if(document.getElementById('prSy')) document.getElementById('prSy').value=d.prSy;
      updateModUI('pr',false); setToggleBtn(d.prA,'btnPRotToggle','PRISM ROT FX','#6f42c1');
      
      if(document.getElementById('colSt')) document.getElementById('colSt').value=d.cSt;
      if(document.getElementById('colEn')) document.getElementById('colEn').value=d.cEn;
      if(document.getElementById('colHo')) document.getElementById('colHo').value=d.cHo;
      if(document.getElementById('colTr')) document.getElementById('colTr').value=d.cTr;
      if(document.getElementById('colSy')) document.getElementById('colSy').value=d.cSy;
      updateColFxUI(false); setToggleBtn(d.cA,'btnColToggle','COLOR CHASER','#fd7e14');

      if(document.getElementById('sgobSt')) document.getElementById('sgobSt').value=d.sgSt;
      if(document.getElementById('sgobEn')) document.getElementById('sgobEn').value=d.sgEn;
      if(document.getElementById('sgobHo')) document.getElementById('sgobHo').value=d.sgHo;
      if(document.getElementById('sgobTr')) document.getElementById('sgobTr').value=d.sgTr;
      if(document.getElementById('sgobSy')) document.getElementById('sgobSy').value=d.sgSy;
      if(document.getElementById('sgobSc')) document.getElementById('sgobSc').value=d.sgSc;
      updateSGobFxUI(false); setToggleBtn(d.sgA,'btnSGobToggle','STATIC GOBO CHASER','#20c997');

      if(document.getElementById('rgobSt')) document.getElementById('rgobSt').value=d.rgSt;
      if(document.getElementById('rgobEn')) document.getElementById('rgobEn').value=d.rgEn;
      if(document.getElementById('rgobHo')) document.getElementById('rgobHo').value=d.rgHo;
      if(document.getElementById('rgobTr')) document.getElementById('rgobTr').value=d.rgTr;
      if(document.getElementById('rgobSy')) document.getElementById('rgobSy').value=d.rgSy;
      if(document.getElementById('rgobSc')) document.getElementById('rgobSc').value=d.rgSc;
      updateRGobFxUI(false); setToggleBtn(d.rgA,'btnRGobToggle','ROTATING GOBO CHASER','#20c997');
      
      if(document.getElementById('cStSlot')) document.getElementById('cStSlot').value=d.chSS;
      if(document.getElementById('cEnSlot')) document.getElementById('cEnSlot').value=d.chES;
      if(document.getElementById('cStSlotL')) document.getElementById('cStSlotL').value=d.chSS;
      if(document.getElementById('cEnSlotL')) document.getElementById('cEnSlotL').value=d.chES;
      if(document.getElementById('cFade')) document.getElementById('cFade').value=d.chF;
      if(document.getElementById('cHold')) document.getElementById('cHold').value=d.chH;
      if(document.getElementById('chaTr')) document.getElementById('chaTr').value=d.chTr;
      if(document.getElementById('chaSy')) document.getElementById('chaSy').value=d.chSy;
      if(document.getElementById('chaOrd')) document.getElementById('chaOrd').value=d.chOrd;
      updateChaserUI(false); updateChaserUIL(false); setToggleBtn(d.chA,'btnChaToggle','SHOW','#ffc107'); setToggleBtn(d.chA,'btnChaToggleL','SHOW','#ffc107');

      if(d.pr > 0) {
        if(document.getElementById('slotSel')) document.getElementById('slotSel').value = d.pr;
      }
      updateSlotInput();
    }
  }

  function sceneAction(act) { 
    const s=document.getElementById("slotSel").value; 
    const n=document.getElementById("slotName").value;
    if(act==='save') {
      if(confirm('SAVE to Preset '+s+'?')) fetch(`/save?slot=${s}&n=${encodeURIComponent(n)}`).then(()=>alert("Saved!")); 
    } else {
      recallPreset(s);
    }
  }
  function recallPreset(s) { fetch(`/recall?slot=${s}`).then(()=>syncUI()); }

  function updateFxUI(t=true) {
    if(document.getElementById('vFxRot')) document.getElementById('vFxRot').innerText=document.getElementById('fxRot').value; 
    if(document.getElementById('vFxSS')) document.getElementById('vFxSS').innerText=document.getElementById('fxSS').value; 
    if(document.getElementById('vFxSE')) document.getElementById('vFxSE').innerText=document.getElementById('fxSE').value; 
    if(document.getElementById('vFxZS')) document.getElementById('vFxZS').innerText=document.getElementById('fxZS').value; 
    if(document.getElementById('vFxZE')) document.getElementById('vFxZE').innerText=document.getElementById('fxZE').value; 
    if(document.getElementById('vFxMS')) document.getElementById('vFxMS').innerText=document.getElementById('fxMS').value;
    if(document.getElementById('fxTr') && document.getElementById('fxTr').value=="1") { document.getElementById('fxSpdContainer').style.display='none'; document.getElementById('fxSyContainer').style.display='block'; } 
    else if(document.getElementById('fxTr')) { document.getElementById('fxSpdContainer').style.display='block'; document.getElementById('fxSyContainer').style.display='none'; }
    if(t&&document.getElementById('btnFxToggle') && document.getElementById('btnFxToggle').innerText.includes("STOP")) toggleFx(1); 
  }
  function toggleFx(fs=-1) {
    let s=fs===-1?(document.getElementById('btnFxToggle').innerText.includes("START")?1:0):fs; setToggleBtn(s,'btnFxToggle','MOVEMENT FX','#17a2b8');
    let t=document.getElementById('fxType').value,r=document.getElementById('fxRot').value,ss=document.getElementById('fxSS').value,se=document.getElementById('fxSE').value,zs=document.getElementById('fxZS').value,ze=document.getElementById('fxZE').value,mm=document.getElementById('fxMM').value,mc=document.getElementById('fxMC').value,ms=document.getElementById('fxMS').value,tr=document.getElementById('fxTr').value,sy=document.getElementById('fxSy').value;
    fetch(`/fx?a=${s}&t=${t}&r=${r}&ss=${ss}&se=${se}&zs=${zs}&ze=${ze}&mm=${mm}&mc=${mc}&ms=${ms}&tr=${tr}&sy=${sy}`);
  }

  function updateModUI(p,t=true) {
    if(!document.getElementById(p+'St')) return;
    let st = parseInt(document.getElementById(p+'St').value); let en = parseInt(document.getElementById(p+'En').value);
    document.getElementById('v'+p+'St').innerText = (p==='gr'||p==='pr') && st>=135 ? st + ' ' + formatRot(st) : st;
    document.getElementById('v'+p+'En').innerText = (p==='gr'||p==='pr') && en>=135 ? en + ' ' + formatRot(en) : en;
    document.getElementById('v'+p+'Sp').innerText=document.getElementById(p+'Sp').value;
    if(document.getElementById(p+'Tr').value=="1"){document.getElementById(p+'SpContainer').style.display='none';document.getElementById(p+'SyContainer').style.display='block';} 
    else {document.getElementById(p+'SpContainer').style.display='block';document.getElementById(p+'SyContainer').style.display='none';}
    if(t){if(p==='dim'&&document.getElementById('btnDimToggle')&&document.getElementById('btnDimToggle').innerText.includes("STOP")) toggleModFx('dim',1); if(p==='gr'&&document.getElementById('btnGRotToggle')&&document.getElementById('btnGRotToggle').innerText.includes("STOP")) toggleModFx('gr',1); if(p==='pr'&&document.getElementById('btnPRotToggle')&&document.getElementById('btnPRotToggle').innerText.includes("STOP")) toggleModFx('pr',1); } 
  }
  function toggleModFx(p,fs=-1) {
    let bId=p==='dim'?'btnDimToggle':(p==='gr'?'btnGRotToggle':'btnPRotToggle'), lbl=p==='dim'?'DIMMER FX':(p==='gr'?'GOBO ROT FX':'PRISM ROT FX'), col=p==='dim'?'#e83e8c':'#6f42c1';
    let s=fs===-1?(document.getElementById(bId).innerText.includes("START")?1:0):fs; setToggleBtn(s,bId,lbl,col);
    let st=document.getElementById(p+'St').value,en=document.getElementById(p+'En').value,sp=document.getElementById(p+'Sp').value,mo=document.getElementById(p+'Mo').value,cu=document.getElementById(p+'Cu').value,tr=document.getElementById(p+'Tr').value,sy=document.getElementById(p+'Sy').value;
    fetch(`/modfx?pfx=${p}&a=${s}&st=${st}&en=${en}&sp=${sp}&mo=${mo}&cu=${cu}&tr=${tr}&sy=${sy}`);
  }

  function updateColFxUI(t=true) { if(!document.getElementById('colHo'))return; document.getElementById('vColHo').innerText=document.getElementById('colHo').value; if(document.getElementById('colTr').value=="1"){document.getElementById('colHoContainer').style.display='none';document.getElementById('colSyContainer').style.display='block';}else{document.getElementById('colHoContainer').style.display='block';document.getElementById('colSyContainer').style.display='none';} if(t&&document.getElementById('btnColToggle').innerText.includes("STOP")) toggleColFx(1); }
  function toggleColFx(fs=-1) { let s=fs===-1?(document.getElementById('btnColToggle').innerText.includes("START")?1:0):fs; setToggleBtn(s,'btnColToggle','COLOR CHASER','#fd7e14'); let st=document.getElementById('colSt').value,en=document.getElementById('colEn').value,ho=document.getElementById('colHo').value,tr=document.getElementById('colTr').value,sy=document.getElementById('colSy').value; fetch(`/colfx?a=${s}&st=${st}&en=${en}&ho=${ho}&tr=${tr}&sy=${sy}`); }
  function updateSGobFxUI(t=true) { if(!document.getElementById('sgobHo'))return; document.getElementById('vSGobHo').innerText=document.getElementById('sgobHo').value; if(document.getElementById('sgobTr').value=="1"){document.getElementById('sgobHoContainer').style.display='none';document.getElementById('sgobSyContainer').style.display='block';}else{document.getElementById('sgobHoContainer').style.display='block';document.getElementById('sgobSyContainer').style.display='none';} if(t&&document.getElementById('btnSGobToggle').innerText.includes("STOP")) toggleSGobFx(1); }
  function toggleSGobFx(fs=-1) { let s=fs===-1?(document.getElementById('btnSGobToggle').innerText.includes("START")?1:0):fs; setToggleBtn(s,'btnSGobToggle','STATIC GOBO CHASER','#20c997'); let st=document.getElementById('sgobSt').value,en=document.getElementById('sgobEn').value,ho=document.getElementById('sgobHo').value,tr=document.getElementById('sgobTr').value,sy=document.getElementById('sgobSy').value,sc=document.getElementById('sgobSc').value; fetch(`/sgobfx?a=${s}&st=${st}&en=${en}&ho=${ho}&tr=${tr}&sy=${sy}&sc=${sc}`); }
  function updateRGobFxUI(t=true) { if(!document.getElementById('rgobHo'))return; document.getElementById('vRGobHo').innerText=document.getElementById('rgobHo').value; if(document.getElementById('rgobTr').value=="1"){document.getElementById('rgobHoContainer').style.display='none';document.getElementById('rgobSyContainer').style.display='block';}else{document.getElementById('rgobHoContainer').style.display='block';document.getElementById('rgobSyContainer').style.display='none';} if(t&&document.getElementById('btnRGobToggle').innerText.includes("STOP")) toggleRGobFx(1); }
  function toggleRGobFx(fs=-1) { let s=fs===-1?(document.getElementById('btnRGobToggle').innerText.includes("START")?1:0):fs; setToggleBtn(s,'btnRGobToggle','ROTATING GOBO CHASER','#20c997'); let st=document.getElementById('rgobSt').value,en=document.getElementById('rgobEn').value,ho=document.getElementById('rgobHo').value,tr=document.getElementById('rgobTr').value,sy=document.getElementById('rgobSy').value,sc=document.getElementById('rgobSc').value; fetch(`/rgobfx?a=${s}&st=${st}&en=${en}&ho=${ho}&tr=${tr}&sy=${sy}&sc=${sc}`); }
  
  function updateChaserUI(t=true) { if(!document.getElementById('cFade'))return; document.getElementById('vcFade').innerText=document.getElementById('cFade').value; document.getElementById('vcHold').innerText=document.getElementById('cHold').value; if(document.getElementById('chaTr').value=="1"){document.getElementById('chaHoContainer').style.display='none';document.getElementById('chaSyContainer').style.display='block';}else{document.getElementById('chaHoContainer').style.display='block';document.getElementById('chaSyContainer').style.display='none';} document.getElementById('cStSlotL').value=document.getElementById('cStSlot').value; document.getElementById('cEnSlotL').value=document.getElementById('cEnSlot').value; document.getElementById('cFadeL').value=document.getElementById('cFade').value; document.getElementById('cHoldL').value=document.getElementById('cHold').value; document.getElementById('chaTrL').value=document.getElementById('chaTr').value; document.getElementById('chaSyL').value=document.getElementById('chaSy').value; document.getElementById('chaOrdL').value=document.getElementById('chaOrd').value; updateChaserUIL(false); if(t&&document.getElementById('btnChaToggle').innerText.includes("STOP")) toggleChaser(1); }
  function updateChaserUIL(t=true) { if(!document.getElementById('cFadeL'))return; document.getElementById('vcFadeL').innerText=document.getElementById('cFadeL').value; document.getElementById('vcHoldL').innerText=document.getElementById('cHoldL').value; if(document.getElementById('chaTrL').value=="1"){document.getElementById('chaHoContainerL').style.display='none';document.getElementById('chaSyContainerL').style.display='block';}else{document.getElementById('chaHoContainerL').style.display='block';document.getElementById('chaSyContainerL').style.display='none';} document.getElementById('cStSlot').value=document.getElementById('cStSlotL').value; document.getElementById('cEnSlot').value=document.getElementById('cEnSlotL').value; document.getElementById('cFade').value=document.getElementById('cFadeL').value; document.getElementById('cHold').value=document.getElementById('cHoldL').value; document.getElementById('chaTr').value=document.getElementById('chaTrL').value; document.getElementById('chaSy').value=document.getElementById('chaSyL').value; document.getElementById('chaOrd').value=document.getElementById('chaOrdL').value; document.getElementById('vcFade').innerText=document.getElementById('cFade').value; document.getElementById('vcHold').innerText=document.getElementById('cHold').value; if(document.getElementById('chaTr').value=="1"){document.getElementById('chaHoContainer').style.display='none';document.getElementById('chaSyContainer').style.display='block';}else{document.getElementById('chaHoContainer').style.display='block';document.getElementById('chaSyContainer').style.display='none';} if(t&&document.getElementById('btnChaToggleL').innerText.includes("STOP")) toggleChaser(1); }
  function toggleChaser(fs=-1) { let s=fs===-1?(document.getElementById('btnChaToggle').innerText.includes("START")?1:0):fs; setToggleBtn(s,'btnChaToggle','SHOW','#ffc107'); setToggleBtn(s,'btnChaToggleL','SHOW','#ffc107'); let st=document.getElementById('cStSlot').value,en=document.getElementById('cEnSlot').value,fade=document.getElementById('cFade').value,hold=document.getElementById('cHold').value,tr=document.getElementById('chaTr').value,sy=document.getElementById('chaSy').value,ord=document.getElementById('chaOrd').value; fetch(`/chaser?active=${s}&st=${st}&en=${en}&fade=${fade}&hold=${hold}&tr=${tr}&sy=${sy}&o=${ord}`); }
  function saveWiFi() { const s=document.getElementById('w_ssid').value,p=document.getElementById('w_pass').value; if(confirm("Save and Restart?")) fetch(`/set_wifi?s=${encodeURIComponent(s)}&p=${encodeURIComponent(p)}`).then(()=>alert("Restarting... http://movinghead.local")); }

  function initJoystick(jId,sId,pref) {
    const joy=document.getElementById(jId),stick=document.getElementById(sId); if(!joy||!stick) return;
    let jx=0,jy=0,jA=false,jL=null;
    function startJoy(e){jA=true;moveJoy(e);jL=setInterval(applyJoy,40);}
    function endJoy(e){jA=false;clearInterval(jL);stick.style.left='50%';stick.style.top='50%';jx=0;jy=0;}
    function moveJoy(e){
      if(!jA) return; let rect=joy.getBoundingClientRect(),evt=e.touches?e.touches[0]:e;
      let x=evt.clientX-rect.left-rect.width/2,y=evt.clientY-rect.top-rect.height/2,mD=rect.width/2-20; 
      let dist=Math.sqrt(x*x+y*y); if(dist>mD){x=(x/dist)*mD;y=(y/dist)*mD;}
      stick.style.left=(x+rect.width/2)+'px'; stick.style.top=(y+rect.height/2)+'px';
      jx=x/mD; jy=y/mD; 
    }
    function applyJoy(){
      if(jx===0&&jy===0) return;
      let spdEl=document.getElementById(pref+'JoySpd'),crvEl=document.getElementById(pref+'JoyCrv');
      let spd=spdEl?parseInt(spdEl.value):2000; let crv=crvEl?parseFloat(crvEl.value):1.0;
      let pMinEl=document.getElementById(pref+'PanMin'),pMaxEl=document.getElementById(pref+'PanMax'),tMinEl=document.getElementById(pref+'TiltMin'),tMaxEl=document.getElementById(pref+'TiltMax');
      let pMin=pMinEl?parseInt(pMinEl.value)<<8:0,pMax=pMaxEl?(parseInt(pMaxEl.value)<<8)|0xFF:65535,tMin=tMinEl?parseInt(tMinEl.value)<<8:0,tMax=tMaxEl?(parseInt(tMaxEl.value)<<8)|0xFF:65535;
      let mx=Math.pow(Math.abs(jx),crv)*Math.sign(jx); let my=Math.pow(Math.abs(jy),crv)*Math.sign(jy);
      let pD=Math.round(mx*spd),tD=Math.round(my*spd);
      if(joyPanRev)currentPan16+=pD;else currentPan16-=pD; if(joyTiltRev)currentTilt16-=tD;else currentTilt16+=tD;
      currentPan16=Math.max(pMin,Math.min(pMax,currentPan16)); currentTilt16=Math.max(tMin,Math.min(tMax,currentTilt16));
      updateDMX(CH_PAN,currentPan16>>8);updateDMX(CH_PAN_FINE,currentPan16&0xFF);updateDMX(CH_TILT,currentTilt16>>8);updateDMX(CH_TILT_FINE,currentTilt16&0xFF);
    }
    joy.addEventListener('touchstart',startJoy);joy.addEventListener('touchmove',e=>{e.preventDefault();moveJoy(e);},{passive:false});joy.addEventListener('touchend',endJoy);joy.addEventListener('mousedown',startJoy);window.addEventListener('mousemove',moveJoy);window.addEventListener('mouseup',()=>{if(jA)endJoy();});
  }
  document.addEventListener("DOMContentLoaded",()=>{
    initDropdowns();syncUI();initJoystick('joystickLive','stickLive','l');initJoystick('joystickProg','stickProg','p');initJoystick('joystickFoll','stickFoll','f');
    document.querySelectorAll('.in3').forEach(el=>el.addEventListener('input',e=>{currentPan16=(parseInt(e.target.value)<<8)|(currentPan16&0xFF);}));
    document.querySelectorAll('.in15').forEach(el=>el.addEventListener('input',e=>{currentPan16=(currentPan16&0xFF00)|parseInt(e.target.value);}));
    document.querySelectorAll('.in4').forEach(el=>el.addEventListener('input',e=>{currentTilt16=(parseInt(e.target.value)<<8)|(currentTilt16&0xFF);}));
    document.querySelectorAll('.in16').forEach(el=>el.addEventListener('input',e=>{currentTilt16=(currentTilt16&0xFF00)|parseInt(e.target.value);}));
  });
</script>
</head>
<body>
  <h2>PRO FIXTURE CONSOLE</h2>

  <div class="tab-bar">
    <button id="tabBtnLive" class="active" onclick="switchTab('live-tab', this)">LIVE</button>
    <button id="tabBtnFoll" onclick="switchTab('foll-tab', this)">FOLLOWSPOT</button>
    <button id="tabBtnProg" onclick="switchTab('prog-tab', this)">PROGRAMMER</button>
  </div>

  <div id="live-tab" class="tab-content active">
    <div style="display:flex; gap:8px; margin-bottom:15px;">
      <button class="btn-danger bump-btn" style="flex:1;" onpointerdown="sendBump('blackout',1)" onpointerup="sendBump('blackout',0)" onpointercancel="sendBump('blackout',0)">BLACKOUT</button>
      <button class="bump-btn" style="background:#fff; color:#000; flex:1;" onpointerdown="sendBump('strobeF',1)" onpointerup="sendBump('strobeF',0)" onpointercancel="sendBump('strobeF',0)">FAST STRB</button>
      <button class="bump-btn" style="background:#ccc; color:#000; flex:1;" onpointerdown="sendBump('strobe50',1)" onpointerup="sendBump('strobe50',0)" onpointercancel="sendBump('strobe50',0)">50% STRB</button>
      <button class="bump-btn" style="background:#ffc107; color:#000; flex:1;" onpointerdown="sendBump('blinder',1)" onpointerup="sendBump('blinder',0)" onpointercancel="sendBump('blinder',0)">BLINDER</button>
    </div>

    <button class="btn-danger bump-btn" style="width:100%; margin-bottom:15px; padding:15px 0; font-size:1.1em;" onclick="killAllFx()">KILL ALL FX / STOP SHOW</button>

    <div style="display:flex; gap:8px; margin-bottom:20px;">
      <button class="bump-btn" style="background:#007bff; flex:1; padding:15px 0;" onclick="tapTempo()">TAP: <span class="bpm-disp">120</span></button>
      <button class="bump-btn" style="background:#17a2b8; flex:1; padding:15px 0;" onclick="hardSync()">SYNC PHASE</button>
      <button class="bump-btn btn-mic" style="background:#28a745; flex:1; padding:15px 0;" onclick="toggleAudio()">🎤 MIC SYNC</button>
    </div>

    <h3 style="color:#aaa; border-bottom:1px solid #333; text-align:left; margin-bottom:10px;">PRESETS (EXECUTORS)</h3>
    <div class="grid-5">
      <button class="preset-btn" id="pre1" onclick="recallPreset(1)"><span class="p-num">1</span><span class="p-name" id="pn1"></span></button>
      <button class="preset-btn" id="pre2" onclick="recallPreset(2)"><span class="p-num">2</span><span class="p-name" id="pn2"></span></button>
      <button class="preset-btn" id="pre3" onclick="recallPreset(3)"><span class="p-num">3</span><span class="p-name" id="pn3"></span></button>
      <button class="preset-btn" id="pre4" onclick="recallPreset(4)"><span class="p-num">4</span><span class="p-name" id="pn4"></span></button>
      <button class="preset-btn" id="pre5" onclick="recallPreset(5)"><span class="p-num">5</span><span class="p-name" id="pn5"></span></button>
      <button class="preset-btn" id="pre6" onclick="recallPreset(6)"><span class="p-num">6</span><span class="p-name" id="pn6"></span></button>
      <button class="preset-btn" id="pre7" onclick="recallPreset(7)"><span class="p-num">7</span><span class="p-name" id="pn7"></span></button>
      <button class="preset-btn" id="pre8" onclick="recallPreset(8)"><span class="p-num">8</span><span class="p-name" id="pn8"></span></button>
      <button class="preset-btn" id="pre9" onclick="recallPreset(9)"><span class="p-num">9</span><span class="p-name" id="pn9"></span></button>
      <button class="preset-btn" id="pre10" onclick="recallPreset(10)"><span class="p-num">10</span><span class="p-name" id="pn10"></span></button>
    </div>

    <div class="grid-wrapper">
      <div class="section">
        <h3 style="color: #ffc107; border-bottom-color: #ffc107;">AUTO-CHASER (SHOW)</h3>
        <select id="chaOrdL" onchange="updateChaserUIL()" style="margin-bottom: 12px;"><option value="0">Order: Sequential</option><option value="1">Order: Random</option></select>
        <select id="chaTrL" onchange="updateChaserUIL()" style="margin-bottom: 12px;"><option value="0">Trigger: Internal Timer</option><option value="1">Trigger: Global BPM Sync</option></select>
        <div id="chaSyContainerL" style="display:none; margin-bottom: 12px;"><select id="chaSyL" onchange="updateChaserUIL()" class="sync-list"></select></div>
        <div style="display:flex; gap:10px; margin-bottom:12px;"><select id="cStSlotL" onchange="updateChaserUIL()"><option value="0">Slot 1</option><option value="1">Slot 2</option><option value="2">Slot 3</option><option value="3">Slot 4</option><option value="4">Slot 5</option><option value="5">Slot 6</option><option value="6">Slot 7</option><option value="7">Slot 8</option><option value="8">Slot 9</option><option value="9">Slot 10</option></select><span style="color:#aaa; align-self:center;">to</span><select id="cEnSlotL" onchange="updateChaserUIL()"><option value="0">Slot 1</option><option value="1">Slot 2</option><option value="2">Slot 3</option><option value="3" selected>Slot 4</option><option value="4">Slot 5</option><option value="5">Slot 6</option><option value="6">Slot 7</option><option value="7">Slot 8</option><option value="8">Slot 9</option><option value="9">Slot 10</option></select></div>
        <div class="slider-row"><label>Fade Time <span id="vcFadeL" class="val">2000</span> ms</label><input type="range" id="cFadeL" min="0" max="10000" step="100" value="2000" oninput="updateChaserUIL()"></div>
        <div class="slider-row" id="chaHoContainerL"><label>Hold Time (Timer)<span id="vcHoldL" class="val">2000</span> ms</label><input type="range" id="cHoldL" min="0" max="10000" step="100" value="2000" oninput="updateChaserUIL()"></div>
        <button id="btnChaToggleL" class="btn-toggle" style="background:#28a745; font-size:1.3em; padding:20px;" onclick="toggleChaser()">START SHOW</button>
      </div>

      <div class="section">
        <h3>LIVE JOYSTICK</h3>
        <div class="joy-container" id="joystickLive"><div class="stick" id="stickLive"></div></div>
        <div class="btn-group" style="margin-bottom: 15px;"><button class="btn-pan-rev" onclick="toggleJoyRev('pan')">Pan Rev</button><button onclick="resetCenter()" style="background:#17a2b8; color:#000;">CENTER</button><button class="btn-tilt-rev" onclick="toggleJoyRev('tilt')">Tilt Rev</button></div>
        <details>
          <summary>JOYSTICK SPEED & CURVE</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <div class="slider-row"><label>Max Speed <span id="vlJoySpd" class="val">2000</span></label><input type="range" id="lJoySpd" min="100" max="5000" value="2000" oninput="syncJoySettings('l')"></div>
            <div class="slider-row"><label>Curve (Lin->Expo) <span id="vlJoyCrv" class="val">1.5</span></label><input type="range" id="lJoyCrv" min="1" max="3" step="0.1" value="1.5" oninput="syncJoySettings('l')"></div>
          </div>
        </details>
      </div>
    </div>
  </div>

  <div id="foll-tab" class="tab-content">
    <button class="btn-danger bump-btn" style="width:100%; margin-bottom:15px; padding:15px 0; font-size:1.1em; background:#fff; color:#000!important;" onclick="clearBeam()">⚪ OPEN WHITE (CLEAR BEAM)</button>
    <div class="grid-wrapper">
      <div class="section">
        <h3>TRACKING JOYSTICK</h3>
        <div class="joy-container" id="joystickFoll" style="width:250px; height:250px;"><div class="stick" id="stickFoll"></div></div>
        <div class="btn-group" style="margin-bottom: 15px;"><button class="btn-pan-rev" onclick="toggleJoyRev('pan')">Pan Rev</button><button onclick="resetCenter()" style="background:#17a2b8; color:#000;">CENTER</button><button class="btn-tilt-rev" onclick="toggleJoyRev('tilt')">Tilt Rev</button></div>
        <details>
          <summary>JOYSTICK CONSTRAINTS & CURVE</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <div class="slider-row"><label>Max Speed <span id="vfJoySpd" class="val">2000</span></label><input type="range" id="fJoySpd" min="100" max="5000" value="2000" oninput="syncJoySettings('f')"></div>
            <div class="slider-row"><label>Curve (Lin->Expo) <span id="vfJoyCrv" class="val">1.5</span></label><input type="range" id="fJoyCrv" min="1" max="3" step="0.1" value="1.5" oninput="syncJoySettings('f')"></div>
            <hr style="border-color:#333;">
            <div class="slider-row"><label>Pan Min Limit</label><input type="range" id="fPanMin" min="0" max="255" value="0"></div><div class="slider-row"><label>Pan Max Limit</label><input type="range" id="fPanMax" min="0" max="255" value="255"></div>
            <div class="slider-row"><label>Tilt Min Limit</label><input type="range" id="fTiltMin" min="0" max="255" value="0"></div><div class="slider-row"><label>Tilt Max Limit</label><input type="range" id="fTiltMax" min="0" max="255" value="255"></div>
          </div>
        </details>
      </div>

      <div class="section">
        <h3>BEAM CONTROLS</h3>
        <div class="slider-row"><label>Dimmer <span class="val v1">0</span></label><input type="range" class="in1" min="0" max="255" value="0" oninput="updateDMX(1, this.value)"></div>
        <div class="slider-row"><label>Focus <span class="val v13">0</span></label><input type="range" class="in13" min="0" max="255" value="0" oninput="updateDMX(13, this.value)"></div>
        <div class="slider-row"><label>Zoom <span class="val v14">0</span></label><input type="range" class="in14" min="0" max="255" value="0" style="direction: rtl;" oninput="updateDMX(14, this.value)"></div>
        <label>Color</label><select class="col-list" onchange="updateDMX(6, COLORS[this.value].v)" style="margin-bottom:12px;"></select>
        <button class="btn-toggle btn-frost" style="background:#28a745;" onclick="toggleEffect(12, '.btn-frost')">TOGGLE FROST</button>
        
        <details open style="margin-top:20px;">
          <summary style="border: 1px solid #e83e8c; background: #1e1e1e; border-radius: 6px; color: #e83e8c;">SMART DIMMER & AUTO FADE</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #e83e8c; border-top: none; border-radius: 0 0 6px 6px;">
            <div class="slider-row"><label>Dimmer Smoothing (Damping) <span id="vDimSm" class="val">0</span>%</label><input type="range" id="dimSm" min="0" max="100" value="0" oninput="updateDimSmooth(this.value)"></div>
            <hr style="border-color:#333; margin: 15px 0;">
            <h3 style="color:#e83e8c; border-bottom-color:#e83e8c;">AUTO FADE (IN / OUT)</h3>
            <select id="afCurve" style="margin-bottom: 12px;">
              <option value="0">Curve: Linear</option>
              <option value="3" selected>Curve: Sine (Soft)</option>
              <option value="1">Curve: Quadratic (Fast End)</option>
            </select>
            <div class="slider-row"><label>Fade Time <span id="vAfTime" class="val">2000</span> ms</label><input type="range" id="afTime" min="100" max="10000" step="100" value="2000" oninput="document.getElementById('vAfTime').innerText=this.value"></div>
            <button id="btnAutoFade" class="btn-toggle" style="background:#5a1010; font-size:1.3em; padding:20px;" onclick="triggerAutoFade()">FADE OUT (TO BLACK)</button>
          </div>
        </details>
      </div>
    </div>
  </div>

  <div id="prog-tab" class="tab-content">
    
    <div style="display:flex; gap:8px; margin-bottom:20px;">
      <button class="bump-btn" style="background:#007bff; flex:1; padding:15px 0;" onclick="tapTempo()">TAP: <span class="bpm-disp">120</span> BPM</button>
      <button class="bump-btn" style="background:#17a2b8; flex:1; padding:15px 0;" onclick="hardSync()">SYNC PHASE</button>
      <button class="bump-btn btn-mic" style="background:#28a745; flex:1; padding:15px 0;" onclick="toggleAudio()">🎤 MIC SYNC</button>
    </div>

    <div class="grid-wrapper">
      <div class="section">
        <h3>Master & Color</h3>
        <div class="slider-row"><label>Dimmer <span class="val v1">0</span></label><input type="range" class="in1" min="0" max="255" value="0" oninput="updateDMX(1, this.value)"></div>
        <div class="slider-row"><label>Strobe <span class="val v2">0</span></label><input type="range" class="in2" min="0" max="255" value="0" oninput="updateDMX(2, this.value)"></div>
        <label>Color <span class="val v6">0</span></label><div class="compound-row"><select id="base6" onchange="updateCompound(6, true, 'base6')"></select><input type="range" id="off6" min="0" max="0" value="0" oninput="updateCompound(6, false, 'base6')"></div>
      </div>

      <div class="section">
        <h3>Joystick</h3>
        <div class="joy-container" id="joystickProg"><div class="stick" id="stickProg"></div></div>
        <div class="xy-labels" style="margin: 8px 0; font-weight: bold;">Pan: <span class="val v3">127</span> | Tilt: <span class="val v4">127</span></div>
        <div class="btn-group" style="margin-bottom:15px;"><button class="btn-pan-rev" onclick="toggleJoyRev('pan')">Pan Rev</button><button onclick="resetCenter()" style="background:#17a2b8; color:#000;">CENTER</button><button class="btn-tilt-rev" onclick="toggleJoyRev('tilt')">Tilt Rev</button></div>
        <details>
          <summary>JOYSTICK SPEED & CURVE</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <div class="slider-row"><label>Max Speed <span id="vpJoySpd" class="val">2000</span></label><input type="range" id="pJoySpd" min="100" max="5000" value="2000" oninput="syncJoySettings('p')"></div>
            <div class="slider-row"><label>Curve (Lin->Expo) <span id="vpJoyCrv" class="val">1.5</span></label><input type="range" id="pJoyCrv" min="1" max="3" step="0.1" value="1.5" oninput="syncJoySettings('p')"></div>
          </div>
        </details>
        <details>
          <summary>ADVANCED MOTORS</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <div class="slider-row"><label>Speed (CH5) <span class="val v5">0</span></label><input type="range" class="in5" min="0" max="255" value="0" oninput="updateDMX(5, this.value)"></div>
            <div class="slider-row"><label>Pan Fine <span class="val v15">127</span></label><input type="range" class="in15" min="0" max="255" value="127" oninput="updateDMX(15, this.value)"></div>
            <div class="slider-row"><label>Tilt Fine <span class="val v16">127</span></label><input type="range" class="in16" min="0" max="255" value="127" oninput="updateDMX(16, this.value)"></div>
          </div>
        </details>
        <div style="display:none;"><input type="range" class="in3" min="0" max="255" value="127"><input type="range" class="in4" min="0" max="255" value="127"></div>
      </div>

      <div class="section">
        <h3>Gobos</h3>
        <label>Static Gobo (CH7) <span class="val v7">0</span></label><div class="compound-row"><select id="base7" onchange="updateCompound(7, true, 'base7')"></select><input type="range" id="off7" min="0" max="0" value="0" oninput="updateCompound(7, false, 'base7')"></div>
        <label>Rotating Gobo (CH8) <span class="val v8">0</span></label><div class="compound-row"><select id="base8" onchange="updateCompound(8, true, 'base8')"></select><input type="range" id="off8" min="0" max="0" value="0" oninput="updateCompound(8, false, 'base8')"></div>
        <label>Gobo Index/Rot (CH9) <span class="val v9">0</span></label>
        <div class="compound-row">
          <select id="base9" onchange="updateCompound(9, true, 'base9')">
            <option value="0,63">Index 0-63°</option>
            <option value="135,120">Rotation (Rev - Stop - Fwd)</option>
          </select>
          <input type="range" id="off9" min="0" max="0" value="0" oninput="updateCompound(9, false, 'base9')">
        </div>
      </div>

      <div class="section">
        <h3>Optics & Prism</h3>
        <div class="btn-group"><button class="btn-prism" onclick="toggleEffect(10, '.btn-prism')">Prism</button><button class="btn-frost" onclick="toggleEffect(12, '.btn-frost')">Frost</button></div>
        <label>Prism Rotation <span class="val v11">0</span></label>
        <div class="compound-row">
          <select id="base11" onchange="updateCompound(11, true, 'base11')">
            <option value="0,127">Index (Base 0)</option>
            <option value="135,120">Rotation (Rev - Stop - Fwd)</option>
          </select>
          <input type="range" id="off11" min="0" max="0" value="0" oninput="updateCompound(11, false, 'base11')">
        </div>
        <div class="slider-row"><label>Focus <span class="val v13">0</span></label><input type="range" class="in13" min="0" max="255" value="0" oninput="updateDMX(13, this.value)"></div>
        <div class="slider-row"><label>Zoom <span class="val v14">0</span></label><input type="range" class="in14" min="0" max="255" value="0" style="direction: rtl;" oninput="updateDMX(14, this.value)"></div>
        <label>Auto Macros</label><select class="in17" onchange="updateDMX(17, this.value)"><option value="0">None</option><option value="50">Slow Auto</option><option value="200">Sound Auto</option></select>
      </div>
    </div>

    <div class="grid-wrapper">
      <div class="section" style="background: transparent; border: none; padding: 0;">
        <details>
          <summary style="border: 1px solid #444; background: #1e1e1e; border-radius: 6px;">SAVE TO PRESET</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <div style="display:flex; gap:10px; margin-bottom:15px;">
              <select id="slotSel" onchange="updateSlotInput()" style="flex:1; margin-bottom:0;"><option value="1">Slot 1</option><option value="2">Slot 2</option><option value="3">Slot 3</option><option value="4">Slot 4</option><option value="5">Slot 5</option><option value="6">Slot 6</option><option value="7">Slot 7</option><option value="8">Slot 8</option><option value="9">Slot 9</option><option value="10">Slot 10</option></select>
              <input type="text" id="slotName" maxlength="20" placeholder="Name (max 20)" style="flex:2; margin-bottom:0;">
            </div>
            <div class="btn-group">
              <button style="background:#28a745; padding:15px; font-size:1.1em;" onclick="sceneAction('save')">SAVE</button>
              <button style="background:#007bff; padding:15px; font-size:1.1em;" onclick="sceneAction('recall')">RECALL</button>
            </div>
          </div>
        </details>
        
        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #17a2b8; background: #1e1e1e; border-radius: 6px; color: #17a2b8;">MOVEMENT FX</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #17a2b8; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="fxType" onchange="updateFxUI()" style="margin-bottom: 12px;"><option value="1">Circle</option><option value="2">Figure 8</option><option value="3">Clover</option><option value="4">Square</option><option value="5">Star</option><option value="6">Waterwave</option><option value="7">Lissajous (Chaos)</option></select>
            <div class="slider-row"><label>Rotation <span id="vFxRot" class="val">0</span>°</label><input type="range" id="fxRot" min="0" max="360" value="0" oninput="updateFxUI()"></div>
            <h3 style="color:#17a2b8; border-bottom-color:#17a2b8; margin-top:15px;">SIZE & SPEED MODULATOR</h3>
            <select id="fxTr" onchange="updateFxUI()" style="margin-bottom: 8px;"><option value="0">Speed: Manual Modulator</option><option value="1">Speed: Global BPM Sync</option></select>
            <div id="fxSyContainer" style="display:none; margin-bottom: 12px;"><select id="fxSy" onchange="updateFxUI()" class="sync-list"></select></div>
            <select id="fxMM" onchange="updateFxUI()" style="margin-bottom: 8px;"><option value="1">Mode: Up / Down (Ping-Pong)</option><option value="0">Mode: Forward (Sawtooth)</option></select>
            <select id="fxMC" onchange="updateFxUI()" style="margin-bottom: 12px;"><option value="3">Curve: Sine (Soft)</option><option value="0">Curve: Linear</option><option value="1">Curve: Quadratic</option><option value="2">Curve: Cubic</option><option value="4">Curve: Gauss (Flash)</option><option value="5">Curve: Random</option></select>
            <div id="fxSpdContainer"><div class="slider-row"><label>Speed Start <span id="vFxSS" class="val">50</span>%</label><input type="range" id="fxSS" min="1" max="100" value="50" oninput="updateFxUI()"></div><div class="slider-row"><label>Speed End <span id="vFxSE" class="val">50</span>%</label><input type="range" id="fxSE" min="1" max="100" value="50" oninput="updateFxUI()"></div></div>
            <div class="slider-row"><label>Size Start <span id="vFxZS" class="val">30</span>%</label><input type="range" id="fxZS" min="1" max="100" value="30" oninput="updateFxUI()"></div><div class="slider-row"><label>Size End <span id="vFxZE" class="val">30</span>%</label><input type="range" id="fxZE" min="1" max="100" value="30" oninput="updateFxUI()"></div>
            <div class="slider-row"><label>Modulation Speed <span id="vFxMS" class="val">10</span>%</label><input type="range" id="fxMS" min="1" max="100" value="10" oninput="updateFxUI()"></div>
            <button id="btnFxToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleFx()">START MOVEMENT FX</button>
          </div>
        </details>

        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #e83e8c; background: #1e1e1e; border-radius: 6px; color: #e83e8c;">DIMMER FX</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #e83e8c; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="dimMo" onchange="updateModUI('dim')" style="margin-bottom: 8px;"><option value="1">Mode: Up / Down (Ping-Pong)</option><option value="0">Mode: Forward (Sawtooth)</option></select>
            <select id="dimCu" onchange="updateModUI('dim')" style="margin-bottom: 12px;"><option value="3">Curve: Sine (Theater Soft)</option><option value="0">Curve: Linear (Even)</option><option value="1">Curve: Quadratic (Fast End)</option><option value="2">Curve: Cubic (Very Fast)</option><option value="4">Curve: Gauss (Flash)</option><option value="5">Curve: Random</option></select>
            <select id="dimTr" onchange="updateModUI('dim')" style="margin-bottom: 8px;"><option value="0">Speed: Manual Slider</option><option value="1">Speed: Global BPM Sync</option></select>
            <div id="dimSyContainer" style="display:none; margin-bottom: 12px;"><select id="dimSy" onchange="updateModUI('dim')" class="sync-list"></select></div>
            <div class="slider-row"><label>Start Value <span id="vdimSt" class="val">0</span></label><input type="range" id="dimSt" min="0" max="255" value="0" oninput="updateModUI('dim')"></div>
            <div class="slider-row"><label>End Value <span id="vdimEn" class="val">255</span></label><input type="range" id="dimEn" min="0" max="255" value="255" oninput="updateModUI('dim')"></div>
            <div class="slider-row" id="dimSpContainer"><label>Manual Speed <span id="vdimSp" class="val">30</span>%</label><input type="range" id="dimSp" min="1" max="100" value="30" oninput="updateModUI('dim')"></div>
            <button id="btnDimToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleModFx('dim')">START DIMMER FX</button>
          </div>
        </details>

        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #6f42c1; background: #1e1e1e; border-radius: 6px; color: #6f42c1;">ROTATION FX (GOBO & PRISM)</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #6f42c1; border-top: none; border-radius: 0 0 6px 6px;">
            <h3 style="color:#6f42c1; border-bottom-color:#6f42c1;">GOBO ROTATION (CH9)</h3>
            <select id="grMo" onchange="updateModUI('gr')" style="margin-bottom: 8px;"><option value="1">Mode: Up / Down (Ping-Pong)</option><option value="0">Mode: Forward (Sawtooth)</option></select>
            <select id="grCu" onchange="updateModUI('gr')" style="margin-bottom: 12px;"><option value="3">Curve: Sine (Smooth Sweep)</option><option value="0">Curve: Linear</option><option value="1">Curve: Quadratic</option><option value="2">Curve: Cubic</option><option value="4">Curve: Gauss</option><option value="5">Curve: Random</option></select>
            <select id="grTr" onchange="updateModUI('gr')" style="margin-bottom: 8px;"><option value="0">Speed: Manual Slider</option><option value="1">Speed: Global BPM Sync</option></select>
            <div id="grSyContainer" style="display:none; margin-bottom: 12px;"><select id="grSy" onchange="updateModUI('gr')" class="sync-list"></select></div>
            <div class="slider-row"><label>Start DMX <span id="vgrSt" class="val">135</span></label><input type="range" id="grSt" min="135" max="255" value="135" oninput="updateModUI('gr')"></div>
            <div class="slider-row"><label>End DMX <span id="vgrEn" class="val">190</span></label><input type="range" id="grEn" min="135" max="255" value="190" oninput="updateModUI('gr')"></div>
            <div class="slider-row" id="grSpContainer"><label>Manual Speed <span id="vgrSp" class="val">30</span>%</label><input type="range" id="grSp" min="1" max="100" value="30" oninput="updateModUI('gr')"></div>
            <button id="btnGRotToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleModFx('gr')">START GOBO ROT FX</button>

            <h3 style="color:#6f42c1; border-bottom-color:#6f42c1; margin-top:20px;">PRISM ROTATION (CH11)</h3>
            <select id="prMo" onchange="updateModUI('pr')" style="margin-bottom: 8px;"><option value="1">Mode: Up / Down</option><option value="0">Mode: Forward</option></select>
            <select id="prCu" onchange="updateModUI('pr')" style="margin-bottom: 12px;"><option value="3">Curve: Sine</option><option value="0">Curve: Linear</option><option value="1">Curve: Quadratic</option><option value="2">Curve: Cubic</option><option value="4">Curve: Gauss</option><option value="5">Curve: Random</option></select>
            <select id="prTr" onchange="updateModUI('pr')" style="margin-bottom: 8px;"><option value="0">Speed: Manual Slider</option><option value="1">Speed: Global BPM Sync</option></select>
            <div id="prSyContainer" style="display:none; margin-bottom: 12px;"><select id="prSy" onchange="updateModUI('pr')" class="sync-list"></select></div>
            <div class="slider-row"><label>Start DMX <span id="vprSt" class="val">193</span></label><input type="range" id="prSt" min="135" max="255" value="193" oninput="updateModUI('pr')"></div>
            <div class="slider-row"><label>End DMX <span id="vprEn" class="val">255</span></label><input type="range" id="prEn" min="135" max="255" value="255" oninput="updateModUI('pr')"></div>
            <div class="slider-row" id="prSpContainer"><label>Manual Speed <span id="vprSp" class="val">30</span>%</label><input type="range" id="prSp" min="1" max="100" value="30" oninput="updateModUI('pr')"></div>
            <button id="btnPRotToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleModFx('pr')">START PRISM ROT FX</button>
          </div>
        </details>
      </div>

      <div class="section" style="background: transparent; border: none; padding: 0;">
        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #fd7e14; background: #1e1e1e; border-radius: 6px; color: #fd7e14;">COLOR CHASER FX</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #fd7e14; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="colTr" onchange="updateColFxUI()" style="margin-bottom: 12px;"><option value="0">Trigger: Internal Timer</option><option value="1">Trigger: Global BPM Sync</option></select>
            <div id="colSyContainer" style="display:none; margin-bottom: 12px;"><select id="colSy" onchange="updateColFxUI()" class="sync-list"></select></div>
            <div style="display:flex; gap:10px; margin-bottom:12px;"><select id="colSt" onchange="updateColFxUI()" class="col-list"></select><span style="color:#aaa; align-self:center;">to</span><select id="colEn" onchange="updateColFxUI()" class="col-list"></select></div>
            <div class="slider-row" id="colHoContainer"><label>Hold Time (Timer) <span id="vColHo" class="val">1000</span> ms</label><input type="range" id="colHo" min="0" max="10000" step="100" value="1000" oninput="updateColFxUI()"></div>
            <button id="btnColToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleColFx()">START COLOR CHASER</button>
          </div>
        </details>

        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #20c997; background: #1e1e1e; border-radius: 6px; color: #20c997;">STATIC GOBO CHASER</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #20c997; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="sgobTr" onchange="updateSGobFxUI()" style="margin-bottom: 12px;"><option value="0">Trigger: Internal Timer</option><option value="1">Trigger: Global BPM Sync</option></select>
            <div id="sgobSyContainer" style="display:none; margin-bottom: 12px;"><select id="sgobSy" onchange="updateSGobFxUI()" class="sync-list"></select></div>
            <select id="sgobSc" onchange="updateSGobFxUI()" style="margin-bottom: 12px;"><option value="0">Effect: Snap</option><option value="1">Effect: Shake (Wobble)</option></select>
            <div style="display:flex; gap:10px; margin-bottom:12px;"><select id="sgobSt" onchange="updateSGobFxUI()" class="sgob-list"></select><span style="color:#aaa; align-self:center;">to</span><select id="sgobEn" onchange="updateSGobFxUI()" class="sgob-list"></select></div>
            <div class="slider-row" id="sgobHoContainer"><label>Hold Time (Timer) <span id="vSGobHo" class="val">1000</span> ms</label><input type="range" id="sgobHo" min="0" max="10000" step="100" value="1000" oninput="updateSGobFxUI()"></div>
            <button id="btnSGobToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleSGobFx()">START STATIC GOBO</button>
          </div>
        </details>

        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #20c997; background: #1e1e1e; border-radius: 6px; color: #20c997;">ROTATING GOBO CHASER</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #20c997; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="rgobTr" onchange="updateRGobFxUI()" style="margin-bottom: 12px;"><option value="0">Trigger: Internal Timer</option><option value="1">Trigger: Global BPM Sync</option></select>
            <div id="rgobSyContainer" style="display:none; margin-bottom: 12px;"><select id="rgobSy" onchange="updateRGobFxUI()" class="sync-list"></select></div>
            <select id="rgobSc" onchange="updateRGobFxUI()" style="margin-bottom: 12px;"><option value="0">Effect: Snap</option><option value="1">Effect: Shake (Wobble)</option></select>
            <div style="display:flex; gap:10px; margin-bottom:12px;"><select id="rgobSt" onchange="updateRGobFxUI()" class="rgob-list"></select><span style="color:#aaa; align-self:center;">to</span><select id="rgobEn" onchange="updateRGobFxUI()" class="rgob-list"></select></div>
            <div class="slider-row" id="rgobHoContainer"><label>Hold Time (Timer) <span id="vRGobHo" class="val">1000</span> ms</label><input type="range" id="rgobHo" min="0" max="10000" step="100" value="1000" oninput="updateRGobFxUI()"></div>
            <button id="btnRGobToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleRGobFx()">START ROT GOBO</button>
          </div>
        </details>

        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #ffc107; background: #1e1e1e; border-radius: 6px; color: #ffc107;">AUTO-CHASER (SHOW)</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #ffc107; border-top: none; border-radius: 0 0 6px 6px;">
            <select id="chaOrd" onchange="updateChaserUI()" style="margin-bottom: 12px;"><option value="0">Order: Sequential</option><option value="1">Order: Random</option></select>
            <select id="chaTr" onchange="updateChaserUI()" style="margin-bottom: 12px;"><option value="0">Trigger: Internal Timer</option><option value="1">Trigger: Global BPM Sync</option></select>
            <div id="chaSyContainer" style="display:none; margin-bottom: 12px;"><select id="chaSy" onchange="updateChaserUI()" class="sync-list"></select></div>
            <div style="display:flex; gap:10px; margin-bottom:12px;">
              <select id="cStSlot" onchange="updateChaserUI()"><option value="0">Slot 1</option><option value="1">Slot 2</option><option value="2">Slot 3</option><option value="3">Slot 4</option><option value="4">Slot 5</option><option value="5">Slot 6</option><option value="6">Slot 7</option><option value="7">Slot 8</option><option value="8">Slot 9</option><option value="9">Slot 10</option></select>
              <span style="color:#aaa; align-self:center;">to</span>
              <select id="cEnSlot" onchange="updateChaserUI()"><option value="0">Slot 1</option><option value="1">Slot 2</option><option value="2">Slot 3</option><option value="3" selected>Slot 4</option><option value="4">Slot 5</option><option value="5">Slot 6</option><option value="6">Slot 7</option><option value="7">Slot 8</option><option value="8">Slot 9</option><option value="9">Slot 10</option></select>
            </div>
            <div class="slider-row"><label>Fade Time <span id="vcFade" class="val">2000</span> ms</label><input type="range" id="cFade" min="0" max="10000" step="100" value="2000" oninput="updateChaserUI()"></div>
            <div class="slider-row" id="chaHoContainer"><label>Hold Time (Timer)<span id="vcHold" class="val">2000</span> ms</label><input type="range" id="cHold" min="0" max="10000" step="100" value="2000" oninput="updateChaserUI()"></div>
            <button id="btnChaToggle" class="btn-toggle" style="background:#28a745;" onclick="toggleChaser()">START SHOW</button>
          </div>
        </details>
        
        <details style="margin-top: 10px;">
          <summary style="border: 1px solid #444; background: #1e1e1e; border-radius: 6px; color: #fff;">WIFI SETTINGS & UPDATE</summary>
          <div style="padding:15px; background: #1e1e1e; border: 1px solid #333; border-top: none; border-radius: 0 0 6px 6px;">
            <input type="text" id="w_ssid" placeholder="WiFi Name (SSID)">
            <input type="password" id="w_pass" placeholder="WiFi Password">
            <button style="background:#007bff; width:100%; padding:15px; font-weight:bold; border-radius:6px; border:none; color:#fff; margin-bottom: 20px;" onclick="saveWiFi()">SAVE & RESTART</button>
            <form method='POST' action='/update' enctype='multipart/form-data' onsubmit='document.getElementById("updateBtn").style.display="none"; document.getElementById("updateSpinner").style.display="block";'>
              <input type='file' name='update' accept='.bin' required style='margin-bottom:15px; width:100%; color:#fff; font-size: 0.9em;'>
              <input type='submit' id='updateBtn' value='FLASH FIRMWARE' style='background:#17a2b8; width:100%; padding:10px; border:none; border-radius:6px; font-weight:bold; color:white; cursor:pointer;'>
            </form>
            <div id='updateSpinner' style='display:none; text-align:center; color:#00d2ff; font-weight:bold; padding:10px; border: 1px dashed #00d2ff; border-radius:6px;'>
              UPDATE RUNNING... ⏳<br><span style='font-size:0.8em; font-weight:normal; color:#aaa;'>Do not disconnect power!</span>
            </div>
            <button class="btn-danger" style="width:100%; margin-top:20px; padding:15px; border-radius:6px; font-weight:bold;" onclick="if(confirm('Warning: Send reset command?')) updateDMX(18,255)">SYSTEM RESET (CH18)</button>
          </div>
        </details>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";

void triggerSceneFX(int slot) {
  fxActive = chaserScenes[slot].fA; fxType = chaserScenes[slot].fT; fxRot = chaserScenes[slot].fR;
  fxTrigger = chaserScenes[slot].fTr; fxSync = chaserScenes[slot].fSy;
  fxSpdSt = chaserScenes[slot].fSS; fxSpdEn = chaserScenes[slot].fSE; fxSzSt = chaserScenes[slot].fZS; fxSzEn = chaserScenes[slot].fZE;
  fxModMo = chaserScenes[slot].fMM; fxModCu = chaserScenes[slot].fMC; fxModSp = chaserScenes[slot].fMS;
  if(fxActive) { lastFxUpdate = millis(); fxModPhase = 0.0; }

  dimFxActive = chaserScenes[slot].dA; dimFxStart = chaserScenes[slot].dSt; dimFxEnd = chaserScenes[slot].dEn;
  dimFxMode = chaserScenes[slot].dMo; dimFxCurve = chaserScenes[slot].dCu; dimFxSpeed = chaserScenes[slot].dSp;
  dimFxTrigger = chaserScenes[slot].dTr; dimFxSync = chaserScenes[slot].dSy;
  if(dimFxActive) { lastDimFxUpdate = millis(); dimFxPhase = 0.0; }

  gRotFxActive = chaserScenes[slot].grA; gRotFxStart = chaserScenes[slot].grSt; gRotFxEnd = chaserScenes[slot].grEn;
  gRotFxMode = chaserScenes[slot].grMo; gRotFxCurve = chaserScenes[slot].grCu; gRotFxSpeed = chaserScenes[slot].grSp;
  gRotFxTrigger = chaserScenes[slot].grTr; gRotFxSync = chaserScenes[slot].grSy;
  if(gRotFxActive) { lastGRotFxUpdate = millis(); gRotFxPhase = 0.0; }

  pRotFxActive = chaserScenes[slot].prA; pRotFxStart = chaserScenes[slot].prSt; pRotFxEnd = chaserScenes[slot].prEn;
  pRotFxMode = chaserScenes[slot].prMo; pRotFxCurve = chaserScenes[slot].prCu; pRotFxSpeed = chaserScenes[slot].prSp;
  pRotFxTrigger = chaserScenes[slot].prTr; pRotFxSync = chaserScenes[slot].prSy;
  if(pRotFxActive) { lastPRotFxUpdate = millis(); pRotFxPhase = 0.0; }

  colFxActive = chaserScenes[slot].cA; colFxStart = chaserScenes[slot].cSt; colFxEnd = chaserScenes[slot].cEn;
  colFxHoldTime = chaserScenes[slot].cHo; colFxTrigger = chaserScenes[slot].cTr; colFxSync = chaserScenes[slot].cSy;
  if(colFxActive) { colFxStepStartTime = millis(); currentColIdx = colFxStart; if ((colFxStart % 2 == 0 && colFxEnd % 2 == 0) || (colFxStart % 2 != 0 && colFxEnd % 2 != 0)) colFxStep = 2; else colFxStep = 1; }

  sgobFxActive = chaserScenes[slot].sgA; sgobFxStart = chaserScenes[slot].sgSt; sgobFxEnd = chaserScenes[slot].sgEn;
  sgobFxHoldTime = chaserScenes[slot].sgHo; sgobFxTrigger = chaserScenes[slot].sgTr; sgobFxSync = chaserScenes[slot].sgSy; sgobFxScratch = chaserScenes[slot].sgSc;
  if(sgobFxActive) { currentSGobIdx = sgobFxStart; sgobFxStepStartTime = millis(); }

  rgobFxActive = chaserScenes[slot].rgA; rgobFxStart = chaserScenes[slot].rgSt; rgobFxEnd = chaserScenes[slot].rgEn;
  rgobFxHoldTime = chaserScenes[slot].rgHo; rgobFxTrigger = chaserScenes[slot].rgTr; rgobFxSync = chaserScenes[slot].rgSy; rgobFxScratch = chaserScenes[slot].rgSc;
  if(rgobFxActive) { currentRGobIdx = rgobFxStart; rgobFxStepStartTime = millis(); }
}

void onArtDmx(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (universe == 0) {
    chaserActive = false; fxActive = false; dimFxActive = false; colFxActive = false; sgobFxActive = false; rgobFxActive = false; gRotFxActive = false; pRotFxActive = false;
    activePresetSlot = 0;
    for (int i = 0; i < length && i < NUM_CHANNELS; i++) dmxData[i + 1] = data[i];
  }
}

void setup() {
  memset(dmxData, 0, 513);
  dmxData[CH_PAN] = 127; dmxData[CH_TILT] = 127; dmxData[CH_PAN_FINE] = 127; dmxData[CH_TILT_FINE] = 127;
  
  Serial1.begin(250000, SERIAL_8N2, -1, transmitPin); WiFi.setSleep(false); 
  
  prefs.begin("sys", true);
  String sta_ssid = prefs.getString("ssid", ""); String sta_pass = prefs.getString("pass", ""); 
  dimSmoothVal = prefs.getInt("ds", 0);
  prefs.end();

  for(int i=0; i<10; i++) {
    prefs.begin(("sc" + String(i+1)).c_str(), true);
    presetNames[i] = prefs.getString("n", "");
    prefs.end();
  }

  bool connected = false;
  if (sta_ssid != "") { WiFi.mode(WIFI_STA); WiFi.begin(sta_ssid.c_str(), sta_pass.c_str()); int tries = 0; while (WiFi.status() != WL_CONNECTED && tries < 20) { delay(500); tries++; } if (WiFi.status() == WL_CONNECTED) connected = true; }
  if (!connected) { WiFi.mode(WIFI_AP); WiFi.softAP(ap_ssid, ap_password); }

  MDNS.begin("movinghead"); ArduinoOTA.begin(); artnet.begin(); artnet.setArtDmxCallback(onArtDmx);

  server.on("/", []() { server.send_P(200, "text/html", index_html); });
  
  server.on("/api/get_dmx", []() {
    String json = "{";
    for (int i = 1; i <= NUM_CHANNELS; i++) json += "\"" + String(i) + "\":" + String(dmxData[i]) + ",";
    json += "\"cp\":" + String(centerPan16) + ",\"ct\":" + String(centerTilt16) + ",\"bpm\":" + String(globalBPM) + ",\"pr\":" + String(activePresetSlot) + ",\"chA\":" + String(chaserActive ? 1 : 0) + ",";
    
    json += "\"pn\":[";
    for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\""; if(i<9) json += ","; }
    json += "],";

    json += "\"dSm\":" + String(dimSmoothVal) + ",\"fO\":" + String(fadeStateOut ? 1 : 0) + ",";

    json += "\"fA\":" + String(fxActive ? 1 : 0) + ",\"fT\":" + String(fxType) + ",\"fR\":" + String(round(fxRot * (180.0 / PI))) + ",\"fTr\":" + String(fxTrigger) + ",\"fSy\":" + String(fxSync) + ",\"fSS\":" + String(fxSpdSt) + ",\"fSE\":" + String(fxSpdEn) + ",\"fZS\":" + String(fxSzSt) + ",\"fZE\":" + String(fxSzEn) + ",\"fMM\":" + String(fxModMo) + ",\"fMC\":" + String(fxModCu) + ",\"fMS\":" + String(round((fxModSp / 4.0) * 100.0)) + ",";
    json += "\"dA\":" + String(dimFxActive ? 1 : 0) + ",\"dSt\":" + String(dimFxStart) + ",\"dEn\":" + String(dimFxEnd) + ",\"dMo\":" + String(dimFxMode) + ",\"dCu\":" + String(dimFxCurve) + ",\"dSp\":" + String(round((dimFxSpeed / 4.0) * 100.0)) + ",\"dTr\":" + String(dimFxTrigger) + ",\"dSy\":" + String(dimFxSync) + ",";
    json += "\"grA\":" + String(gRotFxActive ? 1 : 0) + ",\"grSt\":" + String(gRotFxStart) + ",\"grEn\":" + String(gRotFxEnd) + ",\"grMo\":" + String(gRotFxMode) + ",\"grCu\":" + String(gRotFxCurve) + ",\"grSp\":" + String(round((gRotFxSpeed / 4.0) * 100.0)) + ",\"grTr\":" + String(gRotFxTrigger) + ",\"grSy\":" + String(gRotFxSync) + ",";
    json += "\"prA\":" + String(pRotFxActive ? 1 : 0) + ",\"prSt\":" + String(pRotFxStart) + ",\"prEn\":" + String(pRotFxEnd) + ",\"prMo\":" + String(pRotFxMode) + ",\"prCu\":" + String(pRotFxCurve) + ",\"prSp\":" + String(round((pRotFxSpeed / 4.0) * 100.0)) + ",\"prTr\":" + String(pRotFxTrigger) + ",\"prSy\":" + String(pRotFxSync) + ",";
    json += "\"cA\":" + String(colFxActive ? 1 : 0) + ",\"cSt\":" + String(colFxStart) + ",\"cEn\":" + String(colFxEnd) + ",\"cHo\":" + String(colFxHoldTime) + ",\"cTr\":" + String(colFxTrigger) + ",\"cSy\":" + String(colFxSync) + ",";
    json += "\"sgA\":" + String(sgobFxActive ? 1 : 0) + ",\"sgSt\":" + String(sgobFxStart) + ",\"sgEn\":" + String(sgobFxEnd) + ",\"sgHo\":" + String(sgobFxHoldTime) + ",\"sgTr\":" + String(sgobFxTrigger) + ",\"sgSy\":" + String(sgobFxSync) + ",\"sgSc\":" + String(sgobFxScratch ? 1 : 0) + ",";
    json += "\"rgA\":" + String(rgobFxActive ? 1 : 0) + ",\"rgSt\":" + String(rgobFxStart) + ",\"rgEn\":" + String(rgobFxEnd) + ",\"rgHo\":" + String(rgobFxHoldTime) + ",\"rgTr\":" + String(rgobFxTrigger) + ",\"rgSy\":" + String(rgobFxSync) + ",\"rgSc\":" + String(rgobFxScratch ? 1 : 0) + ",";
    json += "\"chSS\":" + String(chaserStartSlot) + ",\"chES\":" + String(chaserEndSlot) + ",\"chF\":" + String(fadeTime) + ",\"chH\":" + String(holdTime) + ",\"chTr\":" + String(chaserTrigger) + ",\"chSy\":" + String(chaserSync) + ",\"chOrd\":" + String(chaserOrder);
    json += "}"; server.send(200, "application/json", json);
  });

  server.on("/api/state", []() {
    String json = "{\"pr\":" + String(activePresetSlot) + ",\"bpm\":" + String(globalBPM) + ",\"chA\":" + String(chaserActive ? 1 : 0) + ",\"dA\":" + String(dimFxActive ? 1 : 0) + ",\"fO\":" + String(fadeStateOut ? 1 : 0) + ",\"pn\":[";
    for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\""; if(i<9) json += ","; }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/smooth", []() {
    dimSmoothVal = server.arg("v").toInt();
    prefs.begin("sys", false); prefs.putInt("ds", dimSmoothVal); prefs.end();
    server.send(200, "text/plain", "OK");
  });

  server.on("/autofade", []() {
    fadeDuration = server.arg("t").toInt();
    fadeCurve = server.arg("c").toInt();
    fadeStateOut = !fadeStateOut; 
    fadeStartTime = millis();
    autoFading = true;
    server.send(200, "text/plain", "OK");
  });

  server.on("/set_all", []() {
    chaserActive = false; activePresetSlot = 0;
    for (int i = 1; i <= NUM_CHANNELS; i++) {
      String argName = "c" + String(i);
      if (server.hasArg(argName)) {
        int val = server.arg(argName).toInt();
        if(i == CH_PAN) centerPan16 = (val << 8) | (centerPan16 & 0xFF);
        if(i == CH_PAN_FINE) centerPan16 = (centerPan16 & 0xFF00) | val;
        if(i == CH_TILT) centerTilt16 = (val << 8) | (centerTilt16 & 0xFF);
        if(i == CH_TILT_FINE) centerTilt16 = (centerTilt16 & 0xFF00) | val;
        
        if (i == CH_DIMMER) { dimFxActive = false; dimSmoothTarget = val; }
        if (i == CH_COLOR) colFxActive = false; 
        if (i == CH_GOBO) sgobFxActive = false;
        if (i == CH_GOBO_ROT) rgobFxActive = false;
        if (i == CH_GOBO_IDX) gRotFxActive = false;
        if (i == CH_PRISM_ROT) pRotFxActive = false;
        
        if (!fxActive || (i != CH_PAN && i != CH_TILT && i != CH_PAN_FINE && i != CH_TILT_FINE)) { 
          if(i != CH_DIMMER || dimSmoothVal == 0) dmxData[i] = (byte)val; 
        }
      }
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/set", []() {
    chaserActive = false; activePresetSlot = 0;
    int ch = server.arg("ch").toInt(); int val = server.arg("val").toInt();
    if (ch >= 1 && ch <= NUM_CHANNELS) {
      if(ch == CH_PAN) centerPan16 = (val << 8) | (centerPan16 & 0xFF);
      if(ch == CH_PAN_FINE) centerPan16 = (centerPan16 & 0xFF00) | val;
      if(ch == CH_TILT) centerTilt16 = (val << 8) | (centerTilt16 & 0xFF);
      if(ch == CH_TILT_FINE) centerTilt16 = (centerTilt16 & 0xFF00) | val;
      
      if (ch == CH_DIMMER) { dimFxActive = false; dimSmoothTarget = val; }
      if (ch == CH_COLOR) colFxActive = false;
      if (ch == CH_GOBO) sgobFxActive = false;
      if (ch == CH_GOBO_ROT) rgobFxActive = false;
      if (ch == CH_GOBO_IDX) gRotFxActive = false;
      if (ch == CH_PRISM_ROT) pRotFxActive = false;
      
      if (!fxActive || (ch != CH_PAN && ch != CH_TILT && ch != CH_PAN_FINE && ch != CH_TILT_FINE)) { 
        if(ch != CH_DIMMER || dimSmoothVal == 0) dmxData[ch] = (byte)val; 
      }
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/bump", []() {
    String t = server.arg("t"); bool s = (server.arg("s") == "1");
    if(t == "blinder") bumpBlinder = s; if(t == "strobeF") bumpStrobeF = s; if(t == "strobe50") bumpStrobe50 = s; if(t == "blackout") bumpBlackout = s;
    server.send(200, "text/plain", "OK");
  });

  server.on("/kill_fx", []() {
    fxActive = false; dimFxActive = false; colFxActive = false; sgobFxActive = false; rgobFxActive = false; gRotFxActive = false; pRotFxActive = false; chaserActive = false; activePresetSlot = 0;
    server.send(200, "text/plain", "OK");
  });

  server.on("/bpm", []() { globalBPM = server.arg("v").toInt(); lastBeatTime = millis(); beatTriggered = true; manualTap = true; server.send(200); });
  server.on("/beat", []() { lastBeatTime = millis(); beatTriggered = true; manualTap = true; server.send(200); });
  
  server.on("/sync", []() { 
    dimFxPhase = 0.0; gRotFxPhase = 0.0; pRotFxPhase = 0.0; fxModPhase = 0.0; 
    colFxStepStartTime = millis(); sgobFxStepStartTime = millis(); rgobFxStepStartTime = millis(); stepStartTime = millis();
    lastBeatTime = millis(); beatTriggered = true; manualTap = true;
    server.send(200, "text/plain", "OK"); 
  });

  server.on("/save", []() {
    int s = server.arg("slot").toInt();
    String n = server.arg("n");
    n.replace("\"", "'"); n.replace("\\", "/"); 
    presetNames[s-1] = n;

    prefs.begin(("sc" + String(s)).c_str(), false);
    prefs.putString("n", n);
    for (int i = 1; i <= NUM_CHANNELS; i++) prefs.putUChar(String(i).c_str(), dmxData[i]);
    prefs.putBool("fA", fxActive); prefs.putInt("fT", fxType); prefs.putFloat("fR", fxRot); prefs.putInt("fTr", fxTrigger); prefs.putInt("fSy", fxSync); prefs.putInt("fSS", fxSpdSt); prefs.putInt("fSE", fxSpdEn); prefs.putInt("fZS", fxSzSt); prefs.putInt("fZE", fxSzEn); prefs.putInt("fMM", fxModMo); prefs.putInt("fMC", fxModCu); prefs.putFloat("fMS", fxModSp);
    prefs.putBool("dA", dimFxActive); prefs.putInt("dSt", dimFxStart); prefs.putInt("dEn", dimFxEnd); prefs.putInt("dMo", dimFxMode); prefs.putInt("dCu", dimFxCurve); prefs.putFloat("dSp", dimFxSpeed); prefs.putInt("dTr", dimFxTrigger); prefs.putInt("dSy", dimFxSync);
    prefs.putBool("grA", gRotFxActive); prefs.putInt("grSt", gRotFxStart); prefs.putInt("grEn", gRotFxEnd); prefs.putInt("grMo", gRotFxMode); prefs.putInt("grCu", gRotFxCurve); prefs.putFloat("grSp", gRotFxSpeed); prefs.putInt("grTr", gRotFxTrigger); prefs.putInt("grSy", gRotFxSync);
    prefs.putBool("prA", pRotFxActive); prefs.putInt("prSt", pRotFxStart); prefs.putInt("prEn", pRotFxEnd); prefs.putInt("prMo", pRotFxMode); prefs.putInt("prCu", pRotFxCurve); prefs.putFloat("prSp", pRotFxSpeed); prefs.putInt("prTr", pRotFxTrigger); prefs.putInt("prSy", pRotFxSync);
    prefs.putBool("cA", colFxActive); prefs.putInt("cSt", colFxStart); prefs.putInt("cEn", colFxEnd); prefs.putULong("cHo", colFxHoldTime); prefs.putInt("cTr", colFxTrigger); prefs.putInt("cSy", colFxSync);
    prefs.putBool("sgA", sgobFxActive); prefs.putInt("sgSt", sgobFxStart); prefs.putInt("sgEn", sgobFxEnd); prefs.putULong("sgHo", sgobFxHoldTime); prefs.putInt("sgTr", sgobFxTrigger); prefs.putInt("sgSy", sgobFxSync); prefs.putBool("sgSc", sgobFxScratch);
    prefs.putBool("rgA", rgobFxActive); prefs.putInt("rgSt", rgobFxStart); prefs.putInt("rgEn", rgobFxEnd); prefs.putULong("rgHo", rgobFxHoldTime); prefs.putInt("rgTr", rgobFxTrigger); prefs.putInt("rgSy", rgobFxSync); prefs.putBool("rgSc", rgobFxScratch);
    prefs.end(); server.send(200, "text/plain", "OK");
  });

  server.on("/recall", []() {
    chaserActive = false; int slot = server.arg("slot").toInt(); activePresetSlot = slot;
    prefs.begin(("sc" + String(slot)).c_str(), false);
    for (int i = 1; i <= NUM_CHANNELS; i++) {
      dmxData[i] = prefs.getUChar(String(i).c_str(), 0);
      if(i == CH_PAN) centerPan16 = (dmxData[i] << 8) | (centerPan16 & 0xFF); if(i == CH_PAN_FINE) centerPan16 = (centerPan16 & 0xFF00) | dmxData[i];
      if(i == CH_TILT) centerTilt16 = (dmxData[i] << 8) | (centerTilt16 & 0xFF); if(i == CH_TILT_FINE) centerTilt16 = (centerTilt16 & 0xFF00) | dmxData[i];
    }
    dimSmoothTarget = dmxData[CH_DIMMER]; 
    
    fxActive = prefs.getBool("fA", false); fxType = prefs.getInt("fT", 1); fxRot = prefs.getFloat("fR", 0.0); fxTrigger = prefs.getInt("fTr", 0); fxSync = prefs.getInt("fSy", 2); fxSpdSt = prefs.getInt("fSS", 50); fxSpdEn = prefs.getInt("fSE", 50); fxSzSt = prefs.getInt("fZS", 30); fxSzEn = prefs.getInt("fZE", 30); fxModMo = prefs.getInt("fMM", 1); fxModCu = prefs.getInt("fMC", 0); fxModSp = prefs.getFloat("fMS", 0.5);
    dimFxActive = prefs.getBool("dA", false); dimFxStart = prefs.getInt("dSt", 0); dimFxEnd = prefs.getInt("dEn", 255); dimFxMode = prefs.getInt("dMo", 1); dimFxCurve = prefs.getInt("dCu", 0); dimFxSpeed = prefs.getFloat("dSp", 1.0); dimFxTrigger = prefs.getInt("dTr", 0); dimFxSync = prefs.getInt("dSy", 2);
    gRotFxActive = prefs.getBool("grA", false); gRotFxStart = prefs.getInt("grSt", 0); gRotFxEnd = prefs.getInt("grEn", 255); gRotFxMode = prefs.getInt("grMo", 1); gRotFxCurve = prefs.getInt("grCu", 0); gRotFxSpeed = prefs.getFloat("grSp", 1.0); gRotFxTrigger = prefs.getInt("grTr", 0); gRotFxSync = prefs.getInt("grSy", 2);
    pRotFxActive = prefs.getBool("prA", false); pRotFxStart = prefs.getInt("prSt", 0); pRotFxEnd = prefs.getInt("prEn", 255); pRotFxMode = prefs.getInt("prMo", 1); pRotFxCurve = prefs.getInt("prCu", 0); pRotFxSpeed = prefs.getFloat("prSp", 1.0); pRotFxTrigger = prefs.getInt("prTr", 0); pRotFxSync = prefs.getInt("prSy", 2);
    colFxActive = prefs.getBool("cA", false); colFxStart = prefs.getInt("cSt", 0); colFxEnd = prefs.getInt("cEn", 19); colFxHoldTime = prefs.getULong("cHo", 1000); colFxTrigger = prefs.getInt("cTr", 0); colFxSync = prefs.getInt("cSy", 2);
    sgobFxActive = prefs.getBool("sgA", false); sgobFxStart = prefs.getInt("sgSt", 0); sgobFxEnd = prefs.getInt("sgEn", 9); sgobFxHoldTime = prefs.getULong("sgHo", 1000); sgobFxTrigger = prefs.getInt("sgTr", 0); sgobFxSync = prefs.getInt("sgSy", 2); sgobFxScratch = prefs.getBool("sgSc", false);
    rgobFxActive = prefs.getBool("rgA", false); rgobFxStart = prefs.getInt("rgSt", 0); rgobFxEnd = prefs.getInt("rgEn", 6); rgobFxHoldTime = prefs.getULong("rgHo", 1000); rgobFxTrigger = prefs.getInt("rgTr", 0); rgobFxSync = prefs.getInt("rgSy", 2); rgobFxScratch = prefs.getBool("rgSc", false);
    prefs.end(); 
    if ((colFxStart % 2 == 0 && colFxEnd % 2 == 0) || (colFxStart % 2 != 0 && colFxEnd % 2 != 0)) colFxStep = 2; else colFxStep = 1;
    currentColIdx = colFxStart; currentSGobIdx = sgobFxStart; currentRGobIdx = rgobFxStart;
    server.send(200, "text/plain", "OK");
  });

  server.on("/set_wifi", []() {
    prefs.begin("sys", false); prefs.putString("ssid", server.arg("s")); prefs.putString("pass", server.arg("p")); prefs.end();
    server.send(200, "text/plain", "OK"); delay(500); ESP.restart();
  });

  server.on("/fx", []() {
    fxActive = (server.arg("a") == "1"); fxType = server.arg("t").toInt(); fxRot = server.arg("r").toFloat() * (PI / 180.0); fxSpdSt = server.arg("ss").toInt(); fxSpdEn = server.arg("se").toInt(); fxSzSt = server.arg("zs").toInt(); fxSzEn = server.arg("ze").toInt(); fxModMo = server.arg("mm").toInt(); fxModCu = server.arg("mc").toInt(); fxModSp = (server.arg("ms").toFloat() / 100.0) * 4.0; fxTrigger = server.arg("tr").toInt(); fxSync = server.arg("sy").toInt();
    if(fxActive) { chaserActive = false; lastFxUpdate = millis(); } activePresetSlot = 0; server.send(200, "text/plain", "OK");
  });

  server.on("/modfx", []() {
    String pfx = server.arg("pfx"); bool act = (server.arg("a") == "1"); int st = server.arg("st").toInt(); int en = server.arg("en").toInt(); float sp = (server.arg("sp").toFloat() / 100.0) * 4.0; int mo = server.arg("mo").toInt(); int cu = server.arg("cu").toInt(); int tr = server.arg("tr").toInt(); int sy = server.arg("sy").toInt();
    if (pfx == "dim") { dimFxActive = act; dimFxStart = st; dimFxEnd = en; dimFxSpeed = sp; dimFxMode = mo; dimFxCurve = cu; dimFxTrigger = tr; dimFxSync = sy; if(act) { chaserActive=false; lastDimFxUpdate=millis(); dimFxPhase=0.0; } }
    else if (pfx == "gr") { gRotFxActive = act; gRotFxStart = st; gRotFxEnd = en; gRotFxSpeed = sp; gRotFxMode = mo; gRotFxCurve = cu; gRotFxTrigger = tr; gRotFxSync = sy; if(act) { chaserActive=false; lastGRotFxUpdate=millis(); gRotFxPhase=0.0; } }
    else if (pfx == "pr") { pRotFxActive = act; pRotFxStart = st; pRotFxEnd = en; pRotFxSpeed = sp; pRotFxMode = mo; pRotFxCurve = cu; pRotFxTrigger = tr; pRotFxSync = sy; if(act) { chaserActive=false; lastPRotFxUpdate=millis(); pRotFxPhase=0.0; } }
    activePresetSlot = 0; server.send(200, "text/plain", "OK");
  });

  server.on("/colfx", []() {
    colFxActive = (server.arg("a") == "1"); colFxStart = min(server.arg("st").toInt(), server.arg("en").toInt()); colFxEnd = max(server.arg("st").toInt(), server.arg("en").toInt()); colFxHoldTime = server.arg("ho").toInt(); colFxTrigger = server.arg("tr").toInt(); colFxSync = server.arg("sy").toInt();
    if ((colFxStart % 2 == 0 && colFxEnd % 2 == 0) || (colFxStart % 2 != 0 && colFxEnd % 2 != 0)) colFxStep = 2; else colFxStep = 1;
    if(colFxActive) { chaserActive = false; colFxStepStartTime = millis(); currentColIdx = colFxStart; dmxData[CH_COLOR] = wheelMap[currentColIdx]; }
    activePresetSlot = 0; server.send(200, "text/plain", "OK");
  });

  server.on("/sgobfx", []() {
    sgobFxActive = (server.arg("a") == "1"); sgobFxStart = min(server.arg("st").toInt(), server.arg("en").toInt()); sgobFxEnd = max(server.arg("st").toInt(), server.arg("en").toInt()); sgobFxHoldTime = server.arg("ho").toInt(); sgobFxTrigger = server.arg("tr").toInt(); sgobFxSync = server.arg("sy").toInt(); sgobFxScratch = (server.arg("sc") == "1");
    if(sgobFxActive) { chaserActive = false; sgobFxStepStartTime = millis(); currentSGobIdx = sgobFxStart; dmxData[CH_GOBO] = sGoboMap[currentSGobIdx]; }
    activePresetSlot = 0; server.send(200, "text/plain", "OK");
  });

  server.on("/rgobfx", []() {
    rgobFxActive = (server.arg("a") == "1"); rgobFxStart = min(server.arg("st").toInt(), server.arg("en").toInt()); rgobFxEnd = max(server.arg("st").toInt(), server.arg("en").toInt()); rgobFxHoldTime = server.arg("ho").toInt(); rgobFxTrigger = server.arg("tr").toInt(); rgobFxSync = server.arg("sy").toInt(); rgobFxScratch = (server.arg("sc") == "1");
    if(rgobFxActive) { chaserActive = false; rgobFxStepStartTime = millis(); currentRGobIdx = rgobFxStart; dmxData[CH_GOBO_ROT] = rGoboMap[currentRGobIdx]; }
    activePresetSlot = 0; server.send(200, "text/plain", "OK");
  });

  server.on("/chaser", []() {
    chaserActive = (server.arg("active") == "1"); 
    chaserStartSlot = min(server.arg("st").toInt(), server.arg("en").toInt());
    chaserEndSlot = max(server.arg("st").toInt(), server.arg("en").toInt());
    fadeTime = server.arg("fade").toInt(); holdTime = server.arg("hold").toInt(); chaserTrigger = server.arg("tr").toInt(); chaserSync = server.arg("sy").toInt(); chaserOrder = server.arg("o").toInt();
    
    if(chaserActive) {
      activePresetSlot = 0;
      for(int s=0; s<10; s++) {
        prefs.begin(("sc" + String(s+1)).c_str(), true);
        for (int i = 1; i <= 18; i++) chaserScenes[s].dmx[i] = prefs.getUChar(String(i).c_str(), 0);
        chaserScenes[s].fA = prefs.getBool("fA", false); chaserScenes[s].fT = prefs.getInt("fT", 1); chaserScenes[s].fR = prefs.getFloat("fR", 0.0); chaserScenes[s].fTr = prefs.getInt("fTr", 0); chaserScenes[s].fSy = prefs.getInt("fSy", 2); chaserScenes[s].fSS = prefs.getInt("fSS", 50); chaserScenes[s].fSE = prefs.getInt("fSE", 50); chaserScenes[s].fZS = prefs.getInt("fZS", 30); chaserScenes[s].fZE = prefs.getInt("fZE", 30); chaserScenes[s].fMM = prefs.getInt("fMM", 1); chaserScenes[s].fMC = prefs.getInt("fMC", 0); chaserScenes[s].fMS = prefs.getFloat("fMS", 0.5);
        chaserScenes[s].dA = prefs.getBool("dA", false); chaserScenes[s].dSt = prefs.getInt("dSt", 0); chaserScenes[s].dEn = prefs.getInt("dEn", 255); chaserScenes[s].dMo = prefs.getInt("dMo", 1); chaserScenes[s].dCu = prefs.getInt("dCu", 0); chaserScenes[s].dSp = prefs.getFloat("dSp", 1.0); chaserScenes[s].dTr = prefs.getInt("dTr", 0); chaserScenes[s].dSy = prefs.getInt("dSy", 2);
        chaserScenes[s].grA = prefs.getBool("grA", false); chaserScenes[s].grSt = prefs.getInt("grSt", 0); chaserScenes[s].grEn = prefs.getInt("grEn", 255); chaserScenes[s].grMo = prefs.getInt("grMo", 1); chaserScenes[s].grCu = prefs.getInt("grCu", 0); chaserScenes[s].grSp = prefs.getFloat("grSp", 1.0); chaserScenes[s].grTr = prefs.getInt("grTr", 0); chaserScenes[s].grSy = prefs.getInt("grSy", 2);
        chaserScenes[s].prA = prefs.getBool("prA", false); chaserScenes[s].prSt = prefs.getInt("prSt", 0); chaserScenes[s].prEn = prefs.getInt("prEn", 255); chaserScenes[s].prMo = prefs.getInt("prMo", 1); chaserScenes[s].prCu = prefs.getInt("prCu", 0); chaserScenes[s].prSp = prefs.getFloat("prSp", 1.0); chaserScenes[s].prTr = prefs.getInt("prTr", 0); chaserScenes[s].prSy = prefs.getInt("prSy", 2);
        chaserScenes[s].cA = prefs.getBool("cA", false); chaserScenes[s].cSt = prefs.getInt("cSt", 0); chaserScenes[s].cEn = prefs.getInt("cEn", 19); chaserScenes[s].cHo = prefs.getULong("cHo", 1000); chaserScenes[s].cTr = prefs.getInt("cTr", 0); chaserScenes[s].cSy = prefs.getInt("cSy", 2);
        chaserScenes[s].sgA = prefs.getBool("sgA", false); chaserScenes[s].sgSt = prefs.getInt("sgSt", 0); chaserScenes[s].sgEn = prefs.getInt("sgEn", 9); chaserScenes[s].sgHo = prefs.getULong("sgHo", 1000); chaserScenes[s].sgTr = prefs.getInt("sgTr", 0); chaserScenes[s].sgSy = prefs.getInt("sgSy", 2); chaserScenes[s].sgSc = prefs.getBool("sgSc", false);
        chaserScenes[s].rgA = prefs.getBool("rgA", false); chaserScenes[s].rgSt = prefs.getInt("rgSt", 0); chaserScenes[s].rgEn = prefs.getInt("rgEn", 6); chaserScenes[s].rgHo = prefs.getULong("rgHo", 1000); chaserScenes[s].rgTr = prefs.getInt("rgTr", 0); chaserScenes[s].rgSy = prefs.getInt("rgSy", 2); chaserScenes[s].rgSc = prefs.getBool("rgSc", false);
        prefs.end();
      }
      currentSlot = chaserStartSlot;
      if (chaserOrder == 1) { nextSlot = random(chaserStartSlot, chaserEndSlot + 1); } 
      else { nextSlot = currentSlot + 1; if(nextSlot > chaserEndSlot) nextSlot = chaserStartSlot; }
      inFade = true; stepStartTime = millis(); triggerSceneFX(currentSlot); 
      for (int i = 1; i <= 18; i++) { if (!(i==1 || i==3 || i==4 || i==13 || i==14 || i==15 || i==16)) dmxData[i] = chaserScenes[nextSlot].dmx[i]; }
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close"); server.send(200, "text/plain", (Update.hasError()) ? "Update failed!" : "Update successful. ESP restarting..."); delay(1000); ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) { if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_END) { Update.end(true); }
  });
  server.begin();
}

void loop() {
  ArduinoOTA.handle(); server.handleClient(); artnet.read(); 

  unsigned long now = millis();
  unsigned long beatInterval = 60000 / globalBPM;
  if (now - lastBeatTime >= beatInterval) { lastBeatTime = now; beatTriggered = true; }

  if (manualTap) {
      if (dimFxTrigger == 1) dimFxPhase = 0.0;
      if (gRotFxTrigger == 1) gRotFxPhase = 0.0;
      if (pRotFxTrigger == 1) pRotFxPhase = 0.0;
      manualTap = false;
  }

  // --- 1. MOVEMENT FX ENGINE ---
  if (fxActive) {
    float dt = (now - lastFxUpdate) / 1000.0; lastFxUpdate = now;
    fxModPhase += fxModSp * dt; if (fxModPhase >= 1.0) { fxModPhase -= 1.0; lastFxRand = nextFxRand; nextFxRand = random(0, 1000) / 1000.0; }
    float p = fxModPhase; if (fxModMo == 1) p = (p < 0.5) ? (p * 2.0) : (2.0 - p * 2.0); 
    float v = p;
    switch(fxModCu) { case 0: v=p; break; case 1: v=p*p; break; case 2: v=p*p*p; break; case 3: v=0.5-0.5*cos(p*PI); break; case 4: v=exp(-pow((p-0.5)*6.0, 2)); break; case 5: v=lastFxRand+(nextFxRand-lastFxRand)*p; break; }
    float currentSpd = 0.0; if (fxTrigger == 1) { currentSpd = (globalBPM / 60.0) / syncBeats[fxSync] * TWO_PI; } else { currentSpd = (fxSpdSt + v * (fxSpdEn - fxSpdSt)) / 100.0 * 5.0; }
    float currentSize = (fxSzSt + v * (fxSzEn - fxSzSt)) / 100.0; 
    fxTheta += currentSpd * dt; if (fxTheta >= TWO_PI) fxTheta -= TWO_PI;
    float x = 0, y = 0;
    switch(fxType) { case 1: x=cos(fxTheta); y=sin(fxTheta); break; case 2: x=cos(fxTheta); y=sin(2*fxTheta)/2.0; break; case 3: x=cos(4*fxTheta)*cos(fxTheta); y=cos(4*fxTheta)*sin(fxTheta); break; case 4: x=max(-1.0f, min(1.0f, 1.5f*cos(fxTheta))); y=max(-1.0f, min(1.0f, 1.5f*sin(fxTheta))); break; case 5: x=pow(cos(fxTheta), 3); y=pow(sin(fxTheta), 3); break; case 6: x=cos(fxTheta); y=sin(5*fxTheta)*0.3; break; case 7: x=cos(3*fxTheta); y=sin(4*fxTheta); break; }
    float rotX = x * cos(fxRot) - y * sin(fxRot); float rotY = x * sin(fxRot) + y * cos(fxRot);
    int pOut = constrain(centerPan16 + (rotX * currentSize * 32767.0), 0, 65535); int tOut = constrain(centerTilt16 + (rotY * currentSize * 32767.0), 0, 65535);
    dmxData[CH_PAN] = pOut >> 8; dmxData[CH_PAN_FINE] = pOut & 0xFF; dmxData[CH_TILT] = tOut >> 8; dmxData[CH_TILT_FINE] = tOut & 0xFF;
  }

  // --- 2. MODULATOR MACRO ---
  auto runModulator = [&](bool active, float &phase, unsigned long &lastUpd, int trig, int sync, float spd, int mode, int curve, float &lastRand, float &nextRand, int startVal, int endVal, float &outTarget) {
    if(active) {
      float dt = (now - lastUpd) / 1000.0; lastUpd = now;
      float cSpd = spd; if (trig == 1) cSpd = (globalBPM / 60.0) / syncBeats[sync];
      phase += cSpd * dt; 
      
      if (phase >= 1.0) { phase -= 1.0; lastRand = nextRand; nextRand = random(0, 1000) / 1000.0; }
      
      float p = phase; if (mode == 1) p = (p < 0.5) ? (p * 2.0) : (2.0 - p * 2.0); 
      float v = p;
      switch(curve) { case 0: v=p; break; case 1: v=p*p; break; case 2: v=p*p*p; break; case 3: v=0.5-0.5*cos(p*PI); break; case 4: v=exp(-pow((p-0.5)*6.0, 2)); break; case 5: v=lastRand+(nextRand-lastRand)*p; break; }
      outTarget = constrain(startVal + v * (endVal - startVal), 0, 255);
    }
  };

  // Run Dimmer
  if (dimFxActive) {
    runModulator(true, dimFxPhase, lastDimFxUpdate, dimFxTrigger, dimFxSync, dimFxSpeed, dimFxMode, dimFxCurve, lastDimRand, nextDimRand, dimFxStart, dimFxEnd, dimSmoothTarget);
    dimSmoothCurrent = dimSmoothTarget; // Override Smooth
  } else {
    if (dimSmoothVal > 0) {
      float amt = 1.0 - (dimSmoothVal / 100.0) * 0.95;
      dimSmoothCurrent += (dimSmoothTarget - dimSmoothCurrent) * amt;
    } else {
      dimSmoothCurrent = dimSmoothTarget;
    }
  }

  // Auto Fader Logic
  if (autoFading) {
    float p = (now - fadeStartTime) / (float)fadeDuration;
    if (p >= 1.0) { p = 1.0; autoFading = false; }
    float v = p;
    if (fadeCurve == 1) v = p * p; // Quadratic
    else if (fadeCurve == 3) v = 0.5 - 0.5 * cos(p * PI); // Sine
    
    fadeMultiplier = fadeStateOut ? (1.0 - v) : v; 
  } else {
    fadeMultiplier = fadeStateOut ? 0.0 : 1.0;
  }
  
  dmxData[CH_DIMMER] = constrain(round(dimSmoothCurrent * fadeMultiplier), 0, 255);

  // Run Rotation FX
  float dumpTarg = 0; 
  if(gRotFxActive) { runModulator(true, gRotFxPhase, lastGRotFxUpdate, gRotFxTrigger, gRotFxSync, gRotFxSpeed, gRotFxMode, gRotFxCurve, lastGRotRand, nextGRotRand, gRotFxStart, gRotFxEnd, dumpTarg); dmxData[CH_GOBO_IDX] = round(dumpTarg); }
  if(pRotFxActive) { runModulator(true, pRotFxPhase, lastPRotFxUpdate, pRotFxTrigger, pRotFxSync, pRotFxSpeed, pRotFxMode, pRotFxCurve, lastPRotRand, nextPRotRand, pRotFxStart, pRotFxEnd, dumpTarg); dmxData[CH_PRISM_ROT] = round(dumpTarg); }

  // --- 3. COLOR FX ENGINE ---
  if (colFxActive) {
    bool trg = false; 
    if (colFxTrigger == 1) {
        unsigned long interval = (60000.0 / globalBPM) * syncBeats[colFxSync];
        if (now - colFxStepStartTime >= interval) trg = true;
    } else {
        if (now - colFxStepStartTime >= colFxHoldTime) trg = true;
    }
    if (trg) { colFxStepStartTime = now; currentColIdx += colFxStep; if (currentColIdx > colFxEnd) currentColIdx = colFxStart; dmxData[CH_COLOR] = wheelMap[currentColIdx]; }
  }

  // --- 4. GOBO FX ENGINES ---
  if (sgobFxActive) {
    bool trg = false; 
    if (sgobFxTrigger == 1) {
        unsigned long interval = (60000.0 / globalBPM) * syncBeats[sgobFxSync];
        if (now - sgobFxStepStartTime >= interval) trg = true;
    } else {
        if (now - sgobFxStepStartTime >= sgobFxHoldTime) trg = true;
    }
    if (trg) { 
      sgobFxStepStartTime = now; currentSGobIdx++; if (currentSGobIdx > sgobFxEnd) currentSGobIdx = sgobFxStart; 
      byte v = sGoboMap[currentSGobIdx];
      if(sgobFxScratch) v = constrain(v + 201, 0, 255); // Shake Offset
      dmxData[CH_GOBO] = v;
    }
  }

  if (rgobFxActive) {
    bool trg = false; 
    if (rgobFxTrigger == 1) {
        unsigned long interval = (60000.0 / globalBPM) * syncBeats[rgobFxSync];
        if (now - rgobFxStepStartTime >= interval) trg = true;
    } else {
        if (now - rgobFxStepStartTime >= rgobFxHoldTime) trg = true;
    }
    if (trg) { 
      rgobFxStepStartTime = now; currentRGobIdx++; if (currentRGobIdx > rgobFxEnd) currentRGobIdx = rgobFxStart; 
      byte v = rGoboMap[currentRGobIdx];
      if(rgobFxScratch) v = constrain(v + 183, 0, 255); // Shake Offset
      dmxData[CH_GOBO_ROT] = v;
    }
  }

  // --- 5. SCENE CHASER LOGIC ---
  if (chaserActive) {
    unsigned long elapsed = now - stepStartTime;
    if (inFade) {
      if (elapsed >= fadeTime) {
        inFade = false; stepStartTime = now;
        for (int i = 1; i <= 18; i++) {
            if(i == CH_DIMMER) dimSmoothTarget = chaserScenes[nextSlot].dmx[i];
            else dmxData[i] = chaserScenes[nextSlot].dmx[i]; 
        }
        centerPan16 = (dmxData[CH_PAN] << 8) | dmxData[CH_PAN_FINE]; centerTilt16 = (dmxData[CH_TILT] << 8) | dmxData[CH_TILT_FINE];
        triggerSceneFX(nextSlot);
      } else {
        float progress = (float)elapsed / fadeTime;
        for (int i = 1; i <= 18; i++) { 
            if (i==1) dimSmoothTarget = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; 
            else if (i==13 || i==14) dmxData[i] = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; 
        }
        long startP = (chaserScenes[currentSlot].dmx[CH_PAN] << 8) | chaserScenes[currentSlot].dmx[CH_PAN_FINE]; long endP = (chaserScenes[nextSlot].dmx[CH_PAN] << 8) | chaserScenes[nextSlot].dmx[CH_PAN_FINE];
        centerPan16 = startP + (endP - startP) * progress;
        long startT = (chaserScenes[currentSlot].dmx[CH_TILT] << 8) | chaserScenes[currentSlot].dmx[CH_TILT_FINE]; long endT = (chaserScenes[nextSlot].dmx[CH_TILT] << 8) | chaserScenes[nextSlot].dmx[CH_TILT_FINE];
        centerTilt16 = startT + (endT - startT) * progress;
        if (!fxActive) { dmxData[CH_PAN] = centerPan16 >> 8; dmxData[CH_PAN_FINE] = centerPan16 & 0xFF; dmxData[CH_TILT] = centerTilt16 >> 8; dmxData[CH_TILT_FINE] = centerTilt16 & 0xFF; }
      }
    } else { 
      bool trg = false; 
      if (chaserTrigger == 1) {
          unsigned long interval = (60000.0 / globalBPM) * syncBeats[chaserSync];
          if (elapsed >= interval) trg = true;
      } else {
          if (elapsed >= holdTime) trg = true;
      }
      if (trg) {
        inFade = true; stepStartTime = now; currentSlot = nextSlot; 
        if (chaserOrder == 1) { nextSlot = random(chaserStartSlot, chaserEndSlot + 1); } 
        else { nextSlot++; if (nextSlot > chaserEndSlot) nextSlot = chaserStartSlot; }
        
        activePresetSlot = currentSlot + 1; 
        for (int i = 1; i <= 18; i++) { if (!(i==1 || i==3 || i==4 || i==13 || i==14 || i==15 || i==16)) dmxData[i] = chaserScenes[nextSlot].dmx[i]; }
      }
    }
  }

  beatTriggered = false; 

  // --- DMX COPY & LIVE OVERRIDES ---
  byte outDmx[513]; memcpy(outDmx, dmxData, 513);
  if(bumpBlackout) { outDmx[CH_DIMMER] = 0; }
  else if(bumpBlinder) { outDmx[CH_DIMMER] = 255; outDmx[CH_STROBE] = 255; outDmx[CH_COLOR] = 0; outDmx[CH_GOBO] = 0; outDmx[CH_GOBO_ROT] = 0; }
  else if(bumpStrobeF) { outDmx[CH_DIMMER] = 255; outDmx[CH_STROBE] = 247; } 
  else if(bumpStrobe50){ outDmx[CH_DIMMER] = 255; outDmx[CH_STROBE] = 120; } 

  Serial1.updateBaudRate(96000); Serial1.write(0x00); Serial1.flush();
  Serial1.updateBaudRate(250000); Serial1.write(outDmx, 513); Serial1.flush(); delay(20);
}
