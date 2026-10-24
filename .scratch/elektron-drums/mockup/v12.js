// ---------------- data ----------------
const FAM = [
	{id:'st', n:'Syntakt', groups:[['Digital',['BD MODERN','SD BASIC','CY ALLOY','PC CARBON','SD VINTAGE','CP VINTAGE','SY TONE','SY CHORD','SY TOY','SY BITS','SY SWARM','SP TWINSHOT']]]},
	{id:'md', n:'Machinedrum', groups:[['TRX',['TRX-BD','TRX-SD','TRX-XT','TRX-CP','TRX-RS','TRX-CB','TRX-CH','TRX-OH','TRX-CY','TRX-MA','TRX-CL','TRX-XC','TRX-B2','TRX-S2']],['EFM',['EFM-BD','EFM-SD','EFM-XT','EFM-CP','EFM-RS','EFM-CB','EFM-HH','EFM-CY']],['E12',['E12-BD','E12-SD','E12-HT','E12-LT','E12-CP','E12-RS','E12-CB','E12-CH','E12-OH','E12-RC','E12-CC','E12-BR','E12-TA','E12-TR','E12-SH','E12-BC']],['P-I',['P-I-BD','P-I-SD','P-I-MT','P-I-ML','P-I-MA','P-I-RS','P-I-RC','P-I-CC','P-I-HH']],['GND',['GND-SN','GND-NS','GND-IM','GND-SW','GND-PU']],['ROM',['ROM-01','ROM-02','ROM-03','ROM-04']]]},
	{id:'mc', n:'Model:Cycles', groups:[['Machines',['KICK','SNARE','METAL','PERC','TONE','CHORD']]]},
	{id:'dt', n:'Digitakt II', groups:[['Machines',['ONE SHOT','WERP']]]},
	{id:'ar', n:'Rytm', groups:[['BD',['BD HARD','BD CLASSIC','BD FM','BD PLASTIC','BD SILKY','BD SHARP','BD ACOUSTIC']],['SD',['SD HARD','SD CLASSIC','SD FM','SD NATURAL','SD ACOUSTIC']],['Other',['RS HARD','RS CLASSIC','CP CLASSIC','BT CLASSIC','XT CLASSIC','CH CLASSIC','OH CLASSIC','CY CLASSIC','CB CLASSIC','HH BASIC','CY RIDE','CY METALLIC','NOISE GEN','IMPULSE']]]},
	{id:'an', n:'Analog-approx', plus:1, groups:[['Syntakt analog',['BD ANALOG','SD ANALOG','RS ANALOG','CP ANALOG','BT ANALOG','HH ANALOG','CY ANALOG','CB ANALOG']]]},
];
// Syntakt OS 1.41: the SYN page as the manual (appendix A) orders its eight knobs A-H; long names, short names and
// defaults from the firmware's parameter descriptors (0x4022D59C, group = machine id). [long, short, default, min, max, format]
const T=['Tune','TUNE',0,-24,24], O=['Overdrive','OVER',0], D=d=>['Decay','DEC',d], P=['Punch','PNCH',0,0,1,'onoff'];
const PARAMS = {
	'BD MODERN':[T,['Pitch Sweep','SWEP',16],P,D(64),['Square Modulation','SMOD',10],['Triangle Mod+Fdbk','TMOD',16],['Modulation Env','MENV',24],O],
	'SD BASIC':[T,['Pitch Sweep','SWEP',8],P,D(64),['Inharmonicity','INHM',0],['Mod+Fdbk','MDFB',127],['Modulation Env','MENV',0],O],
	'SD VINTAGE':[T,['Pitch Sweep','SWEP',74],P,D(33),['Inharmonicity','INHM',0],['Freq. Complexity','FCMP',110],['Modulation Env','MENV',80],O],
	'PC CARBON':[['Tune','TUNE',-12,-24,24],['Pitch Sweep','SWEP',35],P,D(32),['Inharmonicity','INHM',0],['Modulation','MOD',26],['Modulation Env','MENV',80],O],
	'CP VINTAGE':[T,['Spacing/Crunch','SPCR',46],P,D(32),['Balance','BAL',25],['Body Character','BODY',24],['Body Envelope','BENV',37],O],
	'CY ALLOY':[T,['Shimmer','SHIM',28],P,D(18),['Radio','RADIO',127],['Modulation','MOD',46],['Modulation Env','MENV',0],O],
	'SY BITS':[T,['Detune','DET',-12,-24,24],['Balance','BAL',102],D(60),['Waveform','WAVE',21],['Sample Rate Redux','SRR',0],['Bit Redux','BR',0],O],
	'SY TONE':[['Tune','TUNE',-12,-24,24],['Mod Ratio','RATIO',4],P,D(127),['Feedback','FDBK',41],['Modulation','MOD',75],['Modulation Env','MENV',76],O],
	'SY CHORD':[T,['Waveform','WAVE',10],P,D(127),['Mode/Chord','TYPE',3,0,37],['Chord/Osc Balance','BAL',96],['Filter Envelope','FENV',64],O],
	'SY TOY':[T,['Form','FORM',24],P,D(60),['Impact','IMP',60],['Brightness','BRIG',110],['Partial Decay','PART',64],O],
	'SY SWARM':[T,['Detune','DET',70],['Oscillator Mix','MIX',127],D(75),['Noise Modulation','NMOD',20],['Detune Animation','ANIM',15],['Fundamental Sub','SUB',2,0,2],O],
	'SP TWINSHOT':[['Tune 1','TUN1',0,-24,24],['Sample 1','SMP1',0,-64,63],['Filter 1','FLT1',0,-64,63],['Decay 1','DEC1',127],['Tune 2','TUN2',0,-24,24],['Sample 2','SMP2',1,-64,63],['Mix','MIX',64],O],
};
// Machinedrum OS 1.63: the eight names its display gives (mdProtocol/mdmachines.cpp, read from the OS image); '' = unused
const MDN = {
	'TRX-BD':'PTCH DEC RAMP RDEC STRT NOIS HARM CLIP','TRX-SD':'PTCH DEC BUMP BENV SNAP TONE TUNE CLIP','TRX-XT':'PTCH DEC RAMP RDEC DAMP DIST DTYP -',
	'TRX-CP':'CLPY TONE HARD RICH RATE ROOM RSIZ RTUN','TRX-RS':'PTCH DEC DIST - - - - -','TRX-CB':'PTCH DEC ENH DAMP TONE BUMP - -',
	'TRX-CH':'GAP DEC HPF LPF MTAL - - -','TRX-OH':'GAP DEC HPF LPF MTAL - - -','TRX-CY':'RICH DEC TOP TTUN SIZE PEAK - -',
	'TRX-MA':'ATT SUS REV DAMP RATL RTYP TONE HARD','TRX-CL':'PTCH DEC DUAL ENH TUNE CLIC - -','TRX-XC':'PTCH DEC RAMP RDEC DAMP DIST DTYP -',
	'TRX-B2':'PTCH DEC RAMP HOLD TICK NOIS DIRT DIST','TRX-S2':'PTCH DEC NOIS NDEC POWR TUNE NTUN NTYP',
	'EFM-BD':'PTCH DEC RAMP RDEC MOD MFRQ MDEC MFB','EFM-SD':'PTCH DEC NOIS NDEC MOD MFRQ MDEC HPF','EFM-XT':'PTCH DEC RAMP RDEC MOD MFRQ MDEC CLIC',
	'EFM-CP':'PTCH DEC CLPS CDEC MOD MFRQ MDEC HPF','EFM-RS':'PTCH DEC MOD HPF SNAR SPTC SDEC SMOD','EFM-CB':'PTCH DEC SNAP FB MOD MFRQ MDEC -',
	'EFM-HH':'PTCH DEC TREM TFRQ MOD MFRQ MDEC FB','EFM-CY':'PTCH DEC FB HPF MOD MFRQ MDEC -',
	'E12-BD':'PTCH DEC SNAP SPLN STRT RTRG RTIM BEND','E12-SD':'PTCH DEC HP RING STRT RTRG RTIM BEND','E12-HT':'PTCH DEC HP HPQ STRT RTRG RTIM BEND',
	'E12-LT':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-CP':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-RS':'PTCH DEC HP RATL STRT RTRG RTIM BEND',
	'E12-CB':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-CH':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-OH':'PTCH DEC HP STOP STRT RTRG RTIM BEND',
	'E12-RC':'PTCH DEC HP BELL STRT RTRG RTIM BEND','E12-CC':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-BR':'PTCH DEC HP REAL STRT RTRG RTIM BEND',
	'E12-TA':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-TR':'PTCH DEC HP HPQ STRT RTRG RTIM BEND','E12-SH':'PTCH DEC HP SLEW STRT RTRG RTIM BEND',
	'E12-BC':'PTCH DEC HP BC STRT RTRG RTIM BEND',
	'P-I-BD':'PTCH DEC HARD HAMR TENS DAMP - -','P-I-SD':'PTCH DEC HARD RING TENS RVOL RDEC -','P-I-MT':'PTCH DEC HARD HAMR TUNE DAMP SIZE POS',
	'P-I-ML':'PTCH DEC HARD TENS - - - -','P-I-MA':'GRNS DEC GLEN - SIZE HARD - -','P-I-RS':'PTCH DEC HARD RING RVOL RDEC - -',
	'P-I-RC':'PTCH DEC HARD RING AG AU BR GRAB','P-I-CC':'PTCH DEC HARD RING AG AU BR GRAB','P-I-HH':'PTCH DEC CLSN RING AG AU BR CLOS',
	'GND-SN':'PTCH DEC RAMP RDEC - - - -','GND-NS':'DEC - - - - - - -','GND-IM':'UP UVAL DOWN DVAL - - - -',
	'GND-SW':'PTC1 DEC RAMP RDEC PTC2 PTC3 SKEW UNIS','GND-PU':'PTC1 DEC RAMP RDEC PTC2 PTC3 WIDT UNIS',
	'ROM':'PTCH DEC HOLD BRR STRT END RTRG RTIM',
};
const MD_UNCONF = ['GND-SW','GND-PU','TRX-S2'];	// not offered in OS 1.63 (mdmachines.cpp): names from MDX X.20
function paramsOf(fam,m){
	if(PARAMS[m]) return PARAMS[m];
	if(fam==='md'){ const n=(MDN[m]||MDN[m.startsWith('ROM')?'ROM':'']||'P1 P2 P3 P4 P5 P6 P7 P8').split(' ');
		return n.map((x,i)=>x==='-'?['','',0]:[x,x,[64,40,20,30,0,64,0,0][i]]); }
	// families whose page is not confirmed: placeholders, marked ?
	const ph={mc:['Pitch','Decay','Color','Shape','Sweep','Contour','','Level'],dt:['Bit Reduction','Tune','Start','Length','Slot','Loop','Level','Play Mode'],
		ar:['Tune','Sweep Time','Sweep Depth','Decay Time','Hold Time','Transient','Waveform','Level'],
		an:['Tune','Decay','Sweep','Snap','Tone','Noise','Body','Level']}[fam];
	return ph.map((x,i)=>[x+' ?',x+'?',[64,80,42,32,100,17,0,110][i]]);
}
const famLabel = {st:'Syntakt digital',md:'Machinedrum OS 1.63',mc:'Model:Cycles',dt:'Digitakt II',ar:'Analog Rytm digital layer',an:'Analog-approx, not firmware'};
const TRK = [
	['st','BD MODERN'],['st','SD BASIC'],['md','TRX-RS'],['st','CP VINTAGE'],['ar','BT CLASSIC'],['md','EFM-BD'],['mc','TONE'],['md','TRX-XT'],
	['ar','CH CLASSIC'],['ar','OH CLASSIC'],['st','CY ALLOY'],['md','TRX-CB'],['st','PC CARBON'],['st','SY TONE'],['an','HH ANALOG'],['st','SP TWINSHOT']];
// a track's sound name exists only once the sound has been saved under a name; '' = never saved (the head band then shows the machine alone)
const SNAMES = ['Winston kick','Dry snare','','Vintage clap','Rytm tom','','Cycle tone','','Rytm CH','Rytm OH','Alloy cym','','','FM tone','',''];
const sndName = i => SNAMES[i] || (short(TRK[i][1])+' '+(i+1));
const CHOKE = TRK.map(()=>new Set()); CHOKE[8].add(9);	// track 9 (closed hat) cuts track 10 (open hat)
const short = m =>{ const t = m.replace(/^(TRX|EFM|E12|P-I|GND)-/,'').split(' ')[0]; return t.slice(0,2); };
// Syntakt digital tracks (manual 10.5.7: Lowpass, Highpass, EQ 1-5, OFF), the DT2's six, the Machinedrum's band filter: 15 types
const FTYPES = [
	['LP','st','Lowpass'],['HP','st','Highpass'],['EQ1','st','EQ, widest'],['EQ2','st','EQ 2'],['EQ3','st','EQ 3'],['EQ4','st','EQ 4'],['EQ5','st','EQ, narrowest'],
	['OFF','st','Off'],['MULTI','dt','Multi-mode'],['LP4','dt','Lowpass 4'],['EQ','dt','Equalizer'],['COMB-','dt','Comb-'],['LEGACY','dt','Legacy LP/HP'],['COMB+','dt','Comb+'],
	['MD','md','Band filter: FLTF, FLTW, FLTQ']];
// drive types (ticket 02): [name, source family, short name, knobs [key, shown label, title]]
const DRIVES = [
	['Off','','OFF',[]],
	['Syntakt drive','st','ST drive',[['Amount','Overdrive','Syntakt overdrive (OVER)']]],
	['MD DIST','md','MD DIST',[['Amount','DIST','Distortion (DIST)']]],
	['MD SRR','md','MD SRR',[['Amount','SRR','Sample rate reduction (SRR)']]],
	['DT2 Overdrive','dt','DT2 OD',[['Amount','Overdrive','Overdrive (OVR)']]],
	['DT2 SRR','dt','DT2 SRR',[['Amount','SRR','Sample rate reduction (SRR)']]]];
const DI = n => DRIVES.findIndex(d=>d[0]===n);
const FTL = n => ({MULTI:'Multi',LEGACY:'Legacy',OFF:'Off'})[n]||n;
// labels in sentence case: the first word as written, later words lower case unless they are codes (all caps, digits)
const sc = s => String(s).split(' ').map((w,i)=>i&&!/^[^a-z]+$|[0-9]/.test(w)?w.toLowerCase():w).join(' ');
const slot = (t,pos,v,v2) => ({t:DI(t),pos,v:v??40,v2:v2??64,mix:127});
// a family's own chain, loaded when a track first takes one of its machines (ticket 02: the MD's AMD is not kept; SRR then DIST, both after the filter)
function famChain(f){
	if(f==='md') return {s:[slot('MD SRR','post',0),slot('MD DIST','post',0)], swap:false, band:'md', bpos:'pre', ftype:14};
	if(f==='dt') return {s:[slot('DT2 Overdrive','pre',0),slot('DT2 SRR','post',0)], swap:false, band:'dt', bpos:'pre', ftype:8};
	return {s:[slot('Syntakt drive','pre',20),slot('Off','post')], swap:false, band:'bw', bpos:'pre', ftype:0};
}
const nativeOf = f => f==='md'?'md':(f==='dt'?'dt':'st');	// families without a chain of their own take the Syntakt's (undecided)
let CH = [];	// one chain per track
let state = {track:0, fam:'st', machine:'BD MODERN', lfo:[{shape:1,trig:1,sync:1,sp:13,hz:70},{shape:3,trig:0,sync:1,sp:10,hz:70}], cur:0, env:{a:{a:3,d:40,s:70,r:34}}, ampMode:'adsr'};
const C = () => CH[state.track];

// ---------------- helpers ----------------
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const NS = 'http://www.w3.org/2000/svg';
const col = c => getComputedStyle(document.documentElement).getPropertyValue('--'+c).trim();
const pol = (cx,cy,r,a) => [cx + r*Math.sin(a*Math.PI/180), cy - r*Math.cos(a*Math.PI/180)];
function arc(cx,cy,r,a0,a1){ const [x0,y0]=pol(cx,cy,r,a0),[x1,y1]=pol(cx,cy,r,a1); return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${a1-a0>180?1:0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`; }

// ---------------- knobs ----------------
const KN = {};
function fmt(el,v){
	switch(el.dataset.f){
		case 'pan': return v==0?'C':(v<0?'L'+(-v):'R'+v);
		case 'loop': return v==0?'Off':v;
		case 'mix': return v;
		case 'mult': return [1,2,4,8,16,32,64,128,256,512,1024,2048][v%12];
		case 'onoff': return v?'On':'Off';
		case 'pct': return v;
		case 'dtime': return ({1:'1/128',2:'1/64',3:'1/64.',4:'1/32',6:'1/32.',8:'1/16',12:'1/16.',16:'1/8',24:'1/8.',32:'1/4',48:'1/4.',64:'1/2',96:'1/2.',127:'1'})[v]||v;
		case 'scf': return v==0?'Off':(v<0?'LP'+(-v):'HP'+v);
		case 'hz': { const h=20*Math.pow(1000,v/127); return h<1000?Math.round(h):(h/1000).toFixed(h<10000?1:0)+'k'; }
		case 'db': { const d=v/63*18; return (d>0?'+':'')+d.toFixed(Math.abs(d)<10?1:0); }
		case 'mixpct': return Math.round(v/127*100);
		case 'div': return LDIV[v];
		case 'lhz': { const h=0.05*Math.pow(1000,v/127); return h<1?h.toFixed(2):(h<10?h.toFixed(1):Math.round(h)); }
	}
	return v;
}
// one knob drawing at two sizes: every length is a fixed fraction of the diameter D (KR), so the secondary (52) and
// primary (64) knobs are the same drawing scaled. The value text is KR.text*D: 11 px on the secondary knob, chosen so the
// longest value any parameter takes ('1/128', '1/64T') keeps its ink box 2 px inside the modulation lane, the innermost
// thing the ring draws. No value is ever shrunk alone or moved under its knob: a format or the knob size changes instead.
const KS=52, KP=64;
const KR={ring:0.435, ringW:0.12, modR:0.3425, modW:0.035, dot:0.05, tickIn:0.37, tickOut:0.49, tickW:0.035, disc:0.38, text:11/52, label:12/52};
const inkCtx=document.createElement('canvas').getContext('2d');
function inkOf(s,size,ff){ inkCtx.font=`500 ${size}px ${ff}`; const m=inkCtx.measureText(s);
	return {x0:-m.width/2-m.actualBoundingBoxLeft, x1:-m.width/2+m.actualBoundingBoxRight, a:m.actualBoundingBoxAscent, d:m.actualBoundingBoxDescent}; }
// kept for the callers that repaint after a machine or font change
function harmonize(){ $$('.k').forEach(k=>k.paintVal&&k.paintVal()); }
function makeKnob(el){
	const sz = (+(el.dataset.sz||52))>=60?KP:KS, min=+(el.dataset.min??0), max=+(el.dataset.max??127), c=col(el.dataset.c||"t"), cd=col((el.dataset.c||"t")+"-d");
	const D=sz, r=KR.ring*D, cx=D/2, cy=D/2, fs=+(KR.text*D).toFixed(2), rm=KR.modR*D;
	el.innerHTML = `<span class="l">${sc(el.dataset.dl||el.dataset.l)}${el.dataset.plus?'<span class="plus">+</span>':''}</span><svg width="${D}" height="${D}"><circle cx="${cx}" cy="${cy}" r="${KR.disc*D}" fill="#1b1c1e"/><path class="tk" d="${arc(cx,cy,r,-135,135)}" stroke="${cd}" stroke-width="${KR.ringW*D}" fill="none" stroke-linecap="round"/><path class="vl" stroke="${c}" stroke-width="${KR.ringW*D}" fill="none" stroke-linecap="round"/><circle class="dt" r="${KR.dot*D}" fill="#f2f4f6"/><path class="md" stroke-width="${KR.modW*D}" fill="none" stroke-linecap="butt" style="cursor:ew-resize"/><line class="pd" stroke="#f2f4f6" stroke-width="${KR.tickW*D}" style="display:none"/><text x="${cx}" y="${cy}" text-anchor="middle" font-size="${fs}"></text></svg><b class="pb" title="Modulate or lock" aria-label="Modulate or lock"></b>`;
	el.style.setProperty('--ks',D+'px'); el.style.setProperty('--kl',(KR.label*D).toFixed(2)+'px'); el.style.minWidth=(D+8)+'px'; el.tabIndex=0;
	const vl=el.querySelector('.vl'), dt=el.querySelector('.dt'), tx=el.querySelector('text');
	el.paintVal=()=>{ const ff=getComputedStyle(document.documentElement).getPropertyValue('--fn').trim()||'sans-serif', s=String(fmt(el,el.v));
		tx.textContent=s; const m=inkOf(s,fs,ff); tx.setAttribute('y',(cy+(m.a-m.d)/2).toFixed(2)); };
	const bip = min<0;
	el.set = v => {
		v = Math.max(min,Math.min(max,Math.round(v))); el.v=v;
		const a = -135 + 270*(v-min)/(max-min), a0 = bip ? 0 : -135;
		vl.setAttribute('d', Math.abs(a-a0)<0.5 ? '' : (a>a0?arc(cx,cy,r,a0,a):arc(cx,cy,r,a,a0)));
		const [x,y]=pol(cx,cy,r,a); dt.setAttribute('cx',x); dt.setAttribute('cy',y);
		el.paintVal();
		el.drawMod&&el.drawMod();
		el.dispatchEvent(new CustomEvent('kv',{detail:v}));
	};
	// modulation: a thin solid arc in the modulator's colour just inside the value arc, value -> value + depth;
	// the played value (after modulation) a tick across the ring
	const md=el.querySelector('.md'), pd=el.querySelector('.pd'), ang=v=>-135+270*(Math.max(min,Math.min(max,v))-min)/(max-min);
	el.drawMod=()=>{ const m=(typeof MODS!=='undefined')&&MODS[modKey(el)]; el.mod=m; if(!m){ md.setAttribute('d',''); pd.style.display='none'; return; }
		const a0=ang(el.v), a1=ang(el.v+m.depth*(max-min)/127); md.setAttribute('stroke',LFOC[m.lfo]);
		md.setAttribute('d', Math.abs(a1-a0)<1?'':(a1>a0?arc(cx,cy,rm,a0,a1):arc(cx,cy,rm,a1,a0))); pd.style.display=''; el.animMod(0); };
	el.animMod=t=>{ const m=el.mod; if(!m) return; const pv=el.v+m.depth*(max-min)/127*(0.5+0.5*Math.sin(t/2.5)), a=ang(pv), [x0,y0]=pol(cx,cy,KR.tickIn*D,a), [x1,y1]=pol(cx,cy,KR.tickOut*D,a);
		pd.setAttribute('x1',x0); pd.setAttribute('y1',y0); pd.setAttribute('x2',x1); pd.setAttribute('y2',y1); };
	md.addEventListener('pointerdown',e=>{ e.stopPropagation(); md.setPointerCapture(e.pointerId); const x0=e.clientX, d0=el.mod.depth;
		md.onpointermove=ev=>{ el.mod.depth=Math.max(-64,Math.min(63,Math.round(d0+(ev.clientX-x0)/2))); el.drawMod(); }; md.onpointerup=()=>md.onpointermove=null; });
	el.set(+el.dataset.v);
	el.addEventListener('pointerdown',e=>{
		if(e.target.closest('.pb')){ e.stopPropagation(); openPop(el); return; }
		el.setPointerCapture(e.pointerId); const y0=e.clientY, v0=el.v;
		const lockable = el.closest('.tp') && SEL.size>0;
		if(lockable && el.baseV==null) el.baseV=v0;
		const mv = ev => { el.set(v0 + (y0-ev.clientY)*(max-min)/160); if(lockable){ LRANGE[lkey(el)]=[min,max]; ACTK=lkey(el); SEL.forEach(i=>{ (LOCKS[i]=LOCKS[i]||{})[ACTK]=el.v; const st=STEPS[i]; st.lk=ACTK; if(!st.trig&&!st.tl){ if(ev.altKey) st.tl=true; else st.trig=true; } }); el.classList.add('lk'); buildSteps(); } };
		const up = () => { el.removeEventListener('pointermove',mv); el.removeEventListener('pointerup',up); if(lockable) buildSteps(); };
		el.addEventListener('pointermove',mv); el.addEventListener('pointerup',up);
	});
	if(el.dataset.id) KN[el.dataset.id]=el;
}
function makeFader(el){
	const max=+(el.dataset.max||127), tr=el.querySelector('.tr'), vv=el.querySelector('.v');
	tr.innerHTML='<div class="fill"></div><div class="cap"></div>';
	const fill=tr.querySelector('.fill'), cap=tr.querySelector('.cap');
	el.set = v => { v=Math.max(0,Math.min(max,Math.round(v))); el.v=v; const p=v/max*100; fill.style.height=p+'%'; cap.style.bottom=p+'%'; vv.textContent=v; };
	el.set(+el.dataset.v);
	el.addEventListener('pointerdown',e=>{ el.setPointerCapture(e.pointerId); const y0=e.clientY, v0=el.v, h=tr.clientHeight;
		const mv=ev=>el.set(v0+(y0-ev.clientY)*max/h); const up=()=>{el.removeEventListener('pointermove',mv);el.removeEventListener('pointerup',up)};
		el.addEventListener('pointermove',mv); el.addEventListener('pointerup',up); });
}

// ---------------- segmented + sub-tabs ----------------
document.addEventListener('click',e=>{
	const b = e.target.closest('.seg button, .vseg button');
	if(b && b.parentElement.id!=='ltrig'){ [...b.parentElement.children].forEach(x=>x.classList.remove('on')); b.classList.add('on'); }
	const t = e.target.closest('.hd .tabs button');
	if(t){
		[...t.parentElement.children].forEach(x=>x.classList.remove('on')); t.classList.add('on');
		if(t.dataset.p){ const box=t.closest('.box'); box.querySelectorAll('.pane').forEach(p=>p.classList.toggle('on',p.id===t.dataset.p)); }
	}
});

// ---------------- top tabs / pages ----------------
function show(page){
	const id = page==='track' ? 'track' : page;
	$$('.page').forEach(p=>p.classList.toggle('on',p.id==='p-'+id));
	$('#chain').style.display = page==='track'?'':'none';
	const sq=$('#b-seq'); if(sq) sq.classList.toggle('on', page==='track');
	if(typeof collapse==='function'&&page!=='track'){ collapse(); closePop(); clearGhost(); }
	closeMpop(); redraw(); if(page==='seq') buildParams(); if(page==='browser') buildBrowser(); if(page!=='track') $('#famtoast').classList.remove('on');
}
document.addEventListener('click',e=>{ const g=e.target.closest('[data-go]'); if(g){ $$('.tt').forEach(x=>x.classList.toggle('on',x.textContent==='Seq')); show(g.dataset.go); } });
function buildTracks(){
	const T=$('#tracks'); T.innerHTML='';
	const mk=(txt,cls,fn)=>{const b=document.createElement('button');b.className='tt '+cls;b.textContent=txt;b.onclick=()=>{$$('.tt').forEach(x=>x.classList.remove('on'));b.classList.add('on');fn();};T.appendChild(b);return b;};
	mk('Rainy day','kit',()=>show('kit')).title='Kit';
	TRK.forEach(([f,m],i)=>{ const b=mk(short(m)+' '+(i+1),'',()=>{selectTrack(i);show('track');}); if(i===state.track) b.classList.add('on'); b.dataset.t=i;
		b.innerHTML=`<i class="tl"></i><span class="tn">${short(m)} ${i+1}</span><i class="tpl" title="Play this track (LIVE: records a trig)">&#9654;</i>`;
		b.querySelector('.tpl').addEventListener('click',e=>{ e.stopPropagation(); hitTrack(i); if(document.querySelector('.lockrec.on')&&i===state.track){ STEPS[tick%16].trig=true; buildSteps(); } }); });
	mk('FX','sys',()=>show('fx')); mk('Master','sys',()=>show('master')); mk('Seq','sys',()=>show('seq')).title='Sequencer';
}
$$('.mtab').forEach(b=>b.onclick=()=>{ $$('.mtab').forEach(x=>x.classList.remove('on')); b.classList.add('on');
	if(b.dataset.mtab==='browser') show('browser'); else { const on=$('.tt.on'); on?on.click():show('track'); } });

// ---------------- machine box ----------------
// the machine menu: the head band's machine name opens the family list and the machine grid
let popFam=null;
function closeMpop(){ $$('.mpop,.cpop').forEach(p=>p.remove()); $('#mmenu').classList.remove('on'); }
function openMpop(){
	closeMpop(); popFam=state.fam; const win=$('.win').getBoundingClientRect(), r=$('#mmenu').getBoundingClientRect();
	const p=document.createElement('div'); p.className='mpop'; p.innerHTML='<div class="fl" id="fams"></div><div class="mgrid scroll" id="mgrid"></div>';
	p.style.left=(r.left-win.left)+'px'; p.style.top=(r.bottom-win.top+4)+'px';
	p.addEventListener('pointerdown',e=>e.stopPropagation()); $('.win').appendChild(p); $('#mmenu').classList.add('on');
	buildFams(); buildGrid();
}
function buildFams(){
	const F=$('#fams'); if(!F) return; F.innerHTML='';
	FAM.forEach(f=>{ const b=document.createElement('button'); b.innerHTML=f.n+(f.plus?' <span class="plus">+</span>':''); if(f.id===popFam) b.classList.add('on');
		b.onclick=()=>{ popFam=f.id; buildFams(); buildGrid(); }; F.appendChild(b); });
}
function buildGrid(){
	const G=$('#mgrid'); if(!G) return; G.innerHTML=''; const f=FAM.find(x=>x.id===popFam);
	f.groups.forEach(([gname,list])=>{
		if(f.groups.length>1){ const h=document.createElement('div'); h.className='gh'; h.textContent=gname; G.appendChild(h); }
		const gg=document.createElement('div'); gg.className='gg';
		list.forEach(m=>{ const b=document.createElement('button'); b.className='mb'+(m===state.machine&&f.id===state.fam?' on':''); b.textContent=m; b.onclick=()=>{ closeMpop(); requestMachine(f.id,m); }; gg.appendChild(b); });
		G.appendChild(gg);
	});
}
function setMachine(fam,m){
	state.fam=fam; state.machine=m; TRK[state.track]=[fam,m];
	const p = paramsOf(fam,m);
	const K=$('#mknobs'); K.innerHTML='';
	for(let r=0;r<2;r++){ const row=document.createElement('div'); row.className='kr';
		p.slice(r*4,r*4+4).forEach(([l,sh,v,mn,mx,f])=>{ const k=document.createElement('i'); k.className='k pri'+(l?'':' empty'); k.dataset.l=l||''; k.title=sh?(l===sh?sh:l+' ('+sh+')'):'';
			k.dataset.v=v; if(mn!=null){ k.dataset.min=mn; k.dataset.max=mx; } if(f) k.dataset.f=f; k.dataset.c='k'; k.dataset.sz=64; k.style.setProperty('--kw','112px'); row.appendChild(k); makeKnob(k); k.addEventListener('kv',drawMdisp); });
		K.appendChild(row); }
	$('#mm-fam').textContent = FAM.find(x=>x.id===fam).n; $('#mm-name').innerHTML = m+(fam==='an'?' <span class="plus">+</span>':'')+(MD_UNCONF.includes(m)?' <span class="unc">?</span>':'');
	$('#mmenu').title = famLabel[fam]+(fam==='an'?': approximate circuit model, not firmware':'')+'. Click to choose the machine';
	$('#sname').textContent = SNAMES[state.track]; showChoke();
	const tb=$(`.tt[data-t="${state.track}"]`); if(tb&&tb.querySelector('.tn')) tb.querySelector('.tn').textContent=short(m)+' '+(state.track+1);
	$$('.pad')[state.track] && ($$('.pad')[state.track].textContent=(state.track+1)+':'+short(m));
	buildGrid(); buildFams(); applyChain(); if(typeof MODS!=="undefined"){ buildDest(); applyMods(); } buildSteps(); drawMdisp(); harmonize();
}
function selectTrack(i){ if(typeof closePop==='function'){ closePop(); clearGhost(); } state.track=i; const [f,m]=TRK[i]; setMachine(f,m); $$('.pad').forEach((p,j)=>p.classList.toggle('on',j===i)); }

// ---------------- filter ----------------
const SRCN={st:'Syntakt digital',dt:'Digitakt II',md:'Machinedrum'};
function buildFtypes(){
	const F=$('#ftypes'); F.innerHTML=''; const c=C(), nat=nativeOf(state.fam);
	FTYPES.forEach(([n,src,long],i)=>{ const b=document.createElement('button'); b.className=(src==='dt'?'dt ':src==='md'?'md ':'')+(i===c.ftype?'on':''); b.innerHTML=FTL(n)+(src!==nat?'<span class="plus">+</span>':''); b.title=long+', '+SRCN[src]+(src!==nat?' (another family: +)':'');
		b.onclick=()=>{ C().ftype=i; buildFtypes(); drawFilter(); buildChain(); }; F.appendChild(b); });
	const t=FTYPES[c.ftype][0], md=t==='MD', eq=/^EQ/.test(t);
	if(KN.rs){ KN.fq.querySelector('.l').textContent=md?'Base':'Frequency'; KN.rs.querySelector('.l').textContent=eq&&!md?'Gain':'Resonance'; KN.fw.style.display=md?'':'none'; KN.kt.style.display=md?'none':''; }
}
function magDb(type,x,q){
	const re=1-x*x, im=x/q, den=Math.hypot(re,im);
	let m;
	switch(type){
		case 'LP': case 'LEGACY': case 'MULTI': m=1/den; break;
		case 'HP': m=x*x/den; break;
		case 'OFF': return 0;
		case 'EQ1': case 'EQ2': case 'EQ3': case 'EQ4': case 'EQ5': { const w=[0.7,1.1,1.6,2.4,3.4][+type[2]-1]; return 12*Math.exp(-Math.pow(Math.log2(x)*w,2))*(q/2); }
		case 'EQ': return 12*Math.exp(-Math.pow(Math.log2(x)*2.2,2))*(q/2);
		case 'LP4': m=1/(den*den); break;
		case 'COMB-': case 'COMB+': { const ph=x*Math.PI*6, g=0.4+q/8, s=type==='COMB+'?1:-1; m=Math.hypot(1+s*g*Math.cos(ph), s*g*Math.sin(ph))/(1+g); break; }
		default: m=1/den;
	}
	return 20*Math.log10(Math.max(m,1e-4));
}
function drawFilter(){
	const g=$('#g-filter'), [W,H]=fit(g,500,110), t=FTYPES[C().ftype][0];
	const fq=KN.fq?KN.fq.v:78, rs=KN.rs?KN.rs.v:42, fc=20*Math.pow(1000,fq/127), q=0.6+rs/127*7, wf=Math.pow(2,(KN.fw?KN.fw.v:60)/127*8);
	const y=db=>H*0.38-db*(H/60);
	let d=''; for(let i=0;i<=W;i+=2){ const f=20*Math.pow(1000,i/W);
		let db = t==='MD' ? magDb('HP',f/fc,0.7+q/5)+magDb('LP',f/(fc*wf),0.7+q/5) : magDb(t,f/fc,q);
		db=Math.max(-40,Math.min(20,db)); d+=(i?'L':'M')+i+' '+y(db).toFixed(1); }
	const c=col('s');
	let grid=''; [100,1000,10000].forEach(f=>{const x=Math.log(f/20)/Math.log(1000)*W; grid+=`<line class="gridl" x1="${x}" y1="0" x2="${x}" y2="${H}"/>`;});
	grid+=`<line class="gridl" x1="0" y1="${y(0)}" x2="${W}" y2="${y(0)}"/>`;
	g.innerHTML = grid+`<path d="${d}L${W} ${H}L0 ${H}Z" fill="${c}" fill-opacity=".42"/><path d="${d}" stroke="${c}" stroke-width="1.5" fill="none"/>`;
}

function fit(g,dw,dh){ const W=g.clientWidth||dw, H=g.clientHeight||dh; g.setAttribute('viewBox',`0 0 ${W} ${H}`); return [W,H]; }
// ---------------- band: BASE/WIDTH (Syntakt), EQ (Machinedrum) or the added 3-band EQ ----------------
const hzOf=v=>20*Math.pow(1000,v/127);
function drawBand(){
	const g=$('#g-band'), [W,H]=fit(g,240,110), c=col('b'), m=C().band;
	const y=db=>H/2+6-db*H/46;
	const bell=(f,f0,gdb,w)=>gdb*Math.exp(-Math.pow(Math.log2(f/f0)*w,2));
	const shelf=(f,f0,gdb,hi)=>gdb/(1+Math.pow(f/f0,hi?-2:2));
	let d='';
	for(let i=0;i<=W;i+=2){ const f=20*Math.pow(1000,i/W); let db;
		if(m==='3b'){ const gg=k=>KN[k].v/63*18; db=shelf(f,hzOf(KN.lf.v),gg('lg'),false)+bell(f,hzOf(KN.mf.v),gg('mg'),1.4)+shelf(f,hzOf(KN.hf.v),gg('hg'),true); }
		else if(m==='md'){ db=bell(f,hzOf(KN.eqf.v),KN.eqg.v/64*18,1.1); }
		else { const b=KN.bb.v/127, w=KN.bw.v/127, x=i/W; db=Math.max(-20,(x<b?-18*(b-x)*4:0)+(x>b+w?-18*(x-b-w)*4:0)); }
		d+=(i?'L':'M')+i+' '+y(db).toFixed(1); }
	g.innerHTML=`<line class="gridl" x1="0" y1="${y(0)}" x2="${W}" y2="${y(0)}"/><path d="${d}L${W} ${H}L0 ${H}Z" fill="${c}" fill-opacity=".42"/><path d="${d}" stroke="${c}" stroke-width="1.5" fill="none"/>`;
}
function setBand(m){ C().band=m; $$('#bandsw button').forEach(b=>b.classList.toggle('on',b.dataset.band===m)); ['bw','md','3b'].forEach(x=>$('#bn-'+x).classList.toggle('on',x===(m==='dt'?'bw':m))); drawBand(); buildChain(); }
function setBandPos(p){ C().bpos=p; $$('#bandpos button').forEach(b=>b.classList.toggle('on',b.dataset.bp===p)); buildChain(); }
document.addEventListener('click',e=>{ const b=e.target.closest('#bandpos button'); if(b) setBandPos(b.dataset.bp); });
$('#bandsw').addEventListener('click',e=>{ const b=e.target.closest('button'); if(b) setTimeout(()=>{ setBand(b.dataset.band); if(typeof buildDest==='function') buildDest(); },0); });
function redraw(){ if(!KN.fq) return; drawFilter(); drawEnv(); drawFenv(); drawWave(); drawMdisp(); drawLfo(); drawBand(); if(KN.dtm) drawFx(); }

// ---------------- amp envelope (AHD or ADSR, draggable) and the filter envelope (delay + ADSR, drawn) ----------------
let EW=380,EH=110;
function envPath(p){ return 'M'+p.map(q=>q.map(v=>v.toFixed(1)).join(' ')).join('L'); }
function ampPts(W,H){ const e=state.env.a, top=8, bot=H-6, u=(W-40)/4, sx=v=>v/127*u, x1=8+sx(e.a);
	if(state.ampMode==='ahd'){ const x2=x1+sx(KN.ex?KN.ex.v:0)+2, x3=x2+sx(e.d)+4; return [[6,bot],[x1,top],[x2,top],[x3,bot]]; }
	const x2=x1+sx(e.d)+4, ys=bot-(bot-top)*e.s/127, x3=Math.max(x2+20,W-u-14), x4=Math.min(W-6,x3+sx(e.r)+4); return [[6,bot],[x1,top],[x2,ys],[x3,ys],[x4,bot]]; }
function drawEnv(){
	const g=$('#g-env'); [EW,EH]=fit(g,380,110); const c=col('p'), p=ampPts(EW,EH), d=envPath(p), last=p[p.length-1];
	g.innerHTML=`<line class="gridl" x1="0" y1="${EH/2}" x2="${EW}" y2="${EH/2}"/><path d="${d}L${last[0]} ${EH}L6 ${EH}Z" fill="${c}" fill-opacity=".42"/><path d="${d}" stroke="${c}" stroke-width="2" fill="none"/>`+
		(state.ampMode==='adsr'?p.slice(1).map((q,i)=>`<circle cx="${q[0]}" cy="${q[1]}" r="6" fill="${c}" stroke="#1b1d20" stroke-width="2"/><circle class="hit" data-i="${i+1}" cx="${q[0]}" cy="${q[1]}" r="12" fill="transparent" style="cursor:grab"/>`).join(''):'');
}
$('#g-env').addEventListener('pointerdown',ev=>{
	const c=ev.target.closest('circle'); if(!c) return; const i=+c.dataset.i, g=$('#g-env'); g.setPointerCapture(ev.pointerId);
	const r=g.getBoundingClientRect(), e=state.env.a, p0=ampPts(EW,EH), u=(EW-40)/4, top=8, bot=EH-6;
	const mv=m=>{ const x=m.clientX-r.left, y=m.clientY-r.top, k=v=>Math.max(0,Math.min(127,Math.round(v)));
		if(i===1) e.a=k((x-8)/u*127);
		if(i===2){ e.d=k((x-p0[1][0]-4)/u*127); e.s=k((bot-y)/(bot-top)*127); }
		if(i===3) e.s=k((bot-y)/(bot-top)*127);
		if(i===4) e.r=k((x-p0[3][0]-4)/u*127);
		syncEnvKnobs(); drawEnv(); };
	const up=()=>{g.removeEventListener('pointermove',mv);g.removeEventListener('pointerup',up)};
	g.addEventListener('pointermove',mv); g.addEventListener('pointerup',up);
});
let envSync=false;
function syncEnvKnobs(){ const e=state.env.a; envSync=true; KN.ea.set(e.a); KN.ed.set(e.d); KN.es.set(e.s); KN.er.set(e.r); envSync=false; }
function hookEnv(){ [['ea','a'],['ed','d'],['es','s'],['er','r']].forEach(([id,f])=>KN[id].addEventListener('kv',e=>{ if(envSync) return; state.env.a[f]=e.detail; drawEnv(); })); KN.ex.addEventListener('kv',drawEnv); }
function setAmpMode(m){ state.ampMode=m; KN.es.style.display=KN.er.style.display=m==='ahd'?'none':''; KN.ex.style.display=m==='adsr'?'none':''; drawEnv(); }
$('#envmode').addEventListener('click',e=>{ const b=e.target.closest('button'); if(b) setAmpMode(b.dataset.m); });
function drawFenv(){ const g=$('#g-fenv'); if(!g) return; const [W,H]=fit(g,120,53), c=col('s');
	const v=k=>KN[k]?KN[k].v:0, top=8, bot=H-6, u=(W-12)/5, sx=x=>x/127*u, x0=4+sx(v('fd')), x1=x0+sx(v('fa'))+1, x2=x1+sx(v('fdc'))+2, ys=bot-(bot-top)*v('fs')/127, x3=Math.max(x2+6,W-u-4), x4=Math.min(W-2,x3+sx(v('fr'))+2);
	const d=envPath([[2,bot],[x0,bot],[x1,top],[x2,ys],[x3,ys],[x4,bot]]);
	g.innerHTML=`<path d="${d}L${x4} ${H}L2 ${H}Z" fill="${c}" fill-opacity=".42"/><path d="${d}" stroke="${c}" stroke-width="1.5" fill="none"/>`; }

// ---------------- sample waveform ----------------
let seed=7; const rnd=()=>{seed=(seed*16807)%2147483647; return seed/2147483647;};
const WAVE=[...Array(240)].map((_,i)=>{ const t=i/240; return (0.15+0.85*Math.exp(-t*3.2))*(0.35+0.65*rnd())*(t<0.62?1:0.25); });
// a waveform as its outline and a quiet fill; only the part that plays (start to end) takes the colour
// markers: start and end solid with a flag at the top (start's points right, end's left), loop dashed with a flag at the bottom
let wcN=0;
function waveSvg(W,H,c,s,e,wave,lp,y0){ wave=wave||WAVE; y0=y0||0; const n=wave.length, mid=y0+H/2, top=[], bot=[], id='wc'+(++wcN);
	wave.forEach((a,i)=>{ const x=i*W/(n-1), h=a*(H-12)/2; top.push([x,mid-h]); bot.unshift([x,mid+h]); });
	const path=pts=>'M'+pts.map(p=>p[0].toFixed(1)+' '+p[1].toFixed(1)).join('L')+'Z', all=path(top.concat(bot));
	const clip=`<clipPath id="${id}"><rect x="${Math.min(s,e)}" y="${y0}" width="${Math.abs(e-s)}" height="${H}"/></clipPath>`;
	const flag=(x,dir)=>`<line x1="${x}" y1="${y0}" x2="${x}" y2="${y0+H}" stroke="${c}" stroke-width="2"/><path d="M${x} ${y0}h${dir*8}v8h${-dir*8}z" fill="${c}"/>`;
	const loop=lp==null?'':`<line x1="${lp}" y1="${y0}" x2="${lp}" y2="${y0+H}" stroke="${c}" stroke-width="1.5" stroke-dasharray="4 3"/><path d="M${lp-5} ${y0+H}l5 -7l5 7z" fill="${c}"/>`;
	return `<defs>${clip}</defs><line class="gridl" x1="0" y1="${mid}" x2="${W}" y2="${mid}"/><path d="${all}" fill="#ffffff" fill-opacity=".05" stroke="#6d7076" stroke-width="1"/>`+
		`<g clip-path="url(#${id})"><path d="${all}" fill="${c}" fill-opacity=".42" stroke="${c}" stroke-width="1.5"/></g>`+loop+flag(s,1)+flag(e,-1); }
function drawWave(){
	const g=$('#g-wave'); if(!g||!g.clientWidth) return; const [W,H]=fit(g,480,200), s=(KN.ss?KN.ss.v:6)/127*W, e=(KN.se?KN.se.v:90)/127*W, l=KN.sl?KN.sl.v:0;
	g.innerHTML = waveSvg(W,H,col('g'),s,e,null,l?l/127*W:null);
}
// ---------------- machine display: the sample a sample machine plays, or the voice's last hit (min/max columns that fade) ----------------
const isSampleMachine=(f,m)=>f==='dt'||m==='SP TWINSHOT'||/^ROM-/.test(m);
function seeded(sd){ return ()=>{ sd=(sd*16807)%2147483647; return sd/2147483647; }; }
function sampleOf(sd,n,dec,tail){ const r=seeded(sd); return [...Array(n)].map((_,i)=>{ const t=i/n; return (0.12+0.88*Math.exp(-t*dec))*(0.35+0.65*r())*(t<tail?1:0.2); }); }
function mval(lab){ const k=$$('#mknobs .k').find(k=>new RegExp('^('+lab+')$').test(k.dataset.l)); return k?k.v:null; }
function drawMdisp(){
	const g=$('#g-mdisp'); if(!g) return; const [W,H]=fit(g,480,130), f=state.fam, m=state.machine, sd=[...m].reduce((a,ch)=>a*31+ch.charCodeAt(0),7)%2147483646+1;
	if(isSampleMachine(f,m)){ const c=col('k');
		if(m==='SP TWINSHOT'){ const h=(H-4)/2; g.innerHTML=waveSvg(W,h,c,4,W*0.78,sampleOf(sd,180,5,0.8))+waveSvg(W,h,c,4,W*0.55,sampleOf(sd+11,180,9,0.6),null,h+4); return; }
		if(f==='dt'){ const s=(mval('Start \\?')??42)/127*W, e=Math.min(W-2,s+(mval('Length \\?')??32)/127*W*2), l=(mval('Loop \\?')??17)/127*W; g.innerHTML=waveSvg(W,H,c,s,e,sampleOf(sd,200,3.5,0.9),Math.min(e-4,s+l)); return; }
		const s=(mval('STRT')??0)/127*W+2, e=Math.max(s+20,(mval('END')??127)/127*W-2); g.innerHTML=waveSvg(W,H,c,s,e,sampleOf(sd,200,4,0.85)); return; }
	// synthesis: the hit rendered as min/max columns, 3 px wide, like MoDule's Hit view
	const r=seeded(sd), dec=(mval('Decay|DEC')??64), len=0.15+dec/127*0.85, N=Math.floor(W/3), mid=H/2, A=H/2-6;
	const kick=/BD|KICK|TOM|BT|LT|HT|MT|XT|TONE/.test(m), noisy=/CH|OH|HH|CY|RC|CC|METAL|NS|SN|RS|CP|CLAP|SD/.test(m)&&!kick;
	let ph=0, d='';
	for(let i=0;i<N;i++){ let lo=0, hi=0;
		for(let j=0;j<8;j++){ const t=(i*8+j)/(N*8), env=Math.exp(-t/len*4)*(t<0.004?t/0.004:1); ph+=(kick?(0.06+0.5*Math.exp(-t*30)):0.35+0.2*Math.sin(i));
			const v=env*(noisy?(0.35*Math.sin(ph)+0.65*(r()*2-1)):Math.sin(ph)); lo=Math.min(lo,v); hi=Math.max(hi,v); }
		d+=`M${i*3+1} ${(mid-hi*A).toFixed(1)}V${(mid-lo*A+0.5).toFixed(1)}`; }
	g.innerHTML=`<title>Last hit</title><line class="gridl" x1="0" y1="${mid}" x2="${W}" y2="${mid}"/><path class="scope" d="${d}" stroke="${col('k')}" stroke-width="2" fill="none"/>`;
}

// ---------------- drive: one box, slot A then slot B in series; PRE/POST is each slot's state, the box never moves ----------------
const SL=['A','B'];
function buildSlots(){
	const c=C(), nat=nativeOf(state.fam);
	[0,1].forEach(i=>{
		const s=c.s[i], D=DRIVES[s.t], d=$('#b-drv'+SL[i]); d.classList.toggle('off',!s.t);
		const sel=`<select title="Drive ${SL[i]} type">${DRIVES.map((n,j)=>`<option ${j===s.t?'selected':''}>${n[0]}${n[1]&&n[1]!==nat?' +':''}</option>`).join('')}</select>`;
		const pp=`<div class="seg pp"><button class="${s.pos==='pre'?'on':''}" data-a="pre" title="Before the filter">Pre</button><button class="${s.pos==='post'?'on':''}" data-a="post" title="After the filter">Post</button></div>`;
		d.innerHTML = `<div class="dln"><b class="dn">${SL[i]}</b>${sel}${pp}</div>`+(s.t
			? `<div class="kr">${D[3].map((k,j)=>`<i class="k" data-l="${k[0]}" data-dl="${k[1]}" title="${k[2]}" data-v="${j?s.v2:s.v}" data-c="a" data-sz="64" style="--kw:96px" data-j="${j}"></i>`).join('')}<i class="k" data-l="Mix" data-plus="1" title="Dry / processed, added (+): 100 is the firmware" data-v="${s.mix}" data-c="a" data-sz="64" style="--kw:96px" data-f="mixpct" data-j="m"></i></div>`
			: '');
		d.querySelector('select').onchange=e=>{ s.t=e.target.selectedIndex; buildSlots(); buildDest(); };
		d.querySelector('.pp').addEventListener('click',e=>{ const a=e.target.dataset.a; if(a) s.pos=a; });
		d.querySelectorAll('.k').forEach(k=>{ makeKnob(k); k.addEventListener('kv',e=>{ const j=k.dataset.j; if(j==='m') s.mix=e.detail; else if(j==='1') s.v2=e.detail; else s.v=e.detail; }); });
	});
	if(typeof applyMods==="function") applyMods(); if(typeof harmonize==="function") harmonize();
}
// ---------------- stages: the fixed skeleton, the drives where they sit ----------------
const BANDN={bw:'Base/width',md:'MD EQ','3b':'3-band EQ',dt:'DT2 B-W'};
const natBand = f => f==='md'?'md':(f==='dt'?'dt':'bw');
function stages(c,f){ const nat=nativeOf(f), out=[];
	const drv=pos=>{ const ids=[0,1].filter(i=>c.s[i].pos===pos); if(c.swap) ids.reverse(); ids.forEach(i=>{ const s=c.s[i], D=DRIVES[s.t]; out.push({to:'b-drive',c:'a',n:'Drive '+SL[i],sm:D[2],off:!s.t,plus:!!s.t&&(D[1]!==nat||s.mix<127)}); }); };
	out.push({to:'b-machine',c:'t',n:'Machine',sm:'+ sample'});
	drv('pre');
	const band={to:'b-band',c:'b',n:'Band',sm:BANDN[c.band],plus:c.band!==natBand(nat)||(c.bpos||'pre')!=='pre'};
	if((c.bpos||'pre')==='pre') out.push(band);
	const ft=FTYPES[c.ftype]; out.push({to:'b-filter',c:'s',n:'Filter',sm:ft[0]==='MD'?'MD filter':FTL(ft[0]),plus:ft[1]!==nat});
	if(c.bpos==='post') out.push(band);
	drv('post');
	out.push({to:'b-amp',c:'p',n:'Amp'}); out.push({to:'b-mixer',c:'w',n:'Sends'});
	return out; }
function additions(){ return stages(C(),state.fam).filter(x=>x.plus).length; }
function chainSummary(c,f){ return stages(c,f).slice(1,-2).filter(x=>!x.off).map(x=>x.sm+(x.plus?' <span class="plus">+</span>':'')).join(', '); }
// the boxes sit in a fixed layout: row 1 = Machine, Drive, Band, Filter; row 2 = Sample layer, Amp, LFO, Sends. Nothing moves.
function buildChain(){ redraw(); }
function applyChain(){ buildSlots(); setBand(C().band); setBandPos(C().bpos||'pre'); buildFtypes(); drawFilter(); $('#b-amp').classList.toggle('mdv',state.fam==='md'); drawEnv(); }
// family change: same family keeps the chain; another family loads its own, asking first if this one was changed
function chainModified(f){ const sig=c=>JSON.stringify([c.s.map(x=>[x.t,x.pos,x.mix]),c.band,c.bpos||'pre',c.ftype]); return sig(C())!==sig(famChain(f)); }
function requestMachine(fam,m){
	const cur=TRK[state.track][0];
	if(fam===cur){ setMachine(fam,m); return; }
	if(!chainModified(cur)){ CH[state.track]=famChain(fam); setMachine(fam,m); return; }
	askFamily(fam,m);
}
function askFamily(fam,m){
	const T=$('#famtoast'), fn=FAM.find(x=>x.id===fam).n, a=additions(), sh=fam==='md'?'MD':fn;
	$('#ft-h').textContent='Load the '+sh+' chain?';
	$('#ft-p').innerHTML=`Track ${state.track+1} goes to <b>${m}</b> (${fn}). Its chain was changed.<br>Now: <b>${chainSummary(C(),state.fam)}</b><br>${sh}: <b>${chainSummary(famChain(fam),fam)}</b>`;
	T.classList.add('on');
	$('#ft-keep').onclick=()=>{ T.classList.remove('on'); setMachine(fam,m); };
	$('#ft-load').onclick=()=>{ T.classList.remove('on'); CH[state.track]=famChain(fam); setMachine(fam,m); };
}

// ---------------- LFO ----------------
const SHAPES=['TRI','SINE','SQR','SAW','EXP','RMP','RND'];
function lfoY(s,t){ t=t-Math.floor(t);
	switch(s){case 0:return t<.5?4*t-1:3-4*t; case 1:return Math.sin(t*2*Math.PI); case 2:return t<.5?1:-1; case 3:return 1-2*t; case 4:return 2*Math.exp(-t*4)-1; case 5:return 2*t-1; case 6:return [0.6,-0.4,0.9,-0.8,0.1,-0.2,0.7,-0.9][Math.floor(t*8)];}}
// LFO speed: SYNC = a division of the host tempo (1/64 to 16 bars, dotted '.' and triplet 'T'), HZ = free rate
const LDIV=['1/64T','1/64','1/32T','1/64.','1/32','1/16T','1/32.','1/16','1/8T','1/16.','1/8','1/4T','1/8.','1/4','1/2T','1/4.','1/2','1/1T','1/2.','1/1','2/1T','1/1.','2/1','2/1.','4/1','8/1','16/1'];
function applySync(){ const L=state.lfo[state.cur], k=KN.lsp; if(!k) return;
	if(k.dataset.lfo!=null&&k.v!=null){ const P=state.lfo[+k.dataset.lfo]; P[k.dataset.f==='div'?'sp':'hz']=k.v; }
	Object.assign(k.dataset, L.sync?{f:'div',min:0,max:LDIV.length-1,v:L.sp}:{f:'lhz',min:0,max:127,v:L.hz}); k.dataset.lfo=state.cur;
	makeKnob(k); if(typeof harmonize==='function') harmonize();
	$$('#lsync button').forEach(b=>b.classList.toggle('on',+b.dataset.s===L.sync)); }
document.addEventListener('click',e=>{ const b=e.target.closest('#lsync button'); if(!b) return; const L=state.lfo[state.cur]; if(+b.dataset.s===L.sync) return;
	L[KN.lsp.dataset.f==='div'?'sp':'hz']=KN.lsp.v; delete KN.lsp.dataset.lfo; L.sync=+b.dataset.s; applySync(); });
function buildShapes(){
	const S=$('#lshapes'); S.innerHTML=''; const L=state.lfo[state.cur];
	SHAPES.forEach((n,i)=>{ const b=document.createElement('button'); b.title=n; if(i===L.shape) b.classList.add('on');
		let d=''; for(let x=0;x<=20;x++){ d+=(x?'L':'M')+x+' '+(7-6*lfoY(i,x/20)).toFixed(1); }
		b.innerHTML=`<svg viewBox="0 0 20 14"><path d="${d}"/></svg>`; b.onclick=()=>{L.shape=i;buildShapes();drawLfo();}; S.appendChild(b); });
	const T=$('#ltrig'); T.querySelectorAll('button').forEach(b=>b.remove());
	['Free','Trig','Hold','One','Half'].forEach((n,i)=>{ const b=document.createElement('button'); b.textContent=n; if(i===L.trig) b.classList.add('on'); b.onclick=()=>{L.trig=i;buildShapes();}; T.appendChild(b); });
}
function drawLfo(){
	const g=$('#g-lfo'), [W,H]=fit(g,330,200), L=state.lfo[state.cur], c=col('y'), dep=(KN.ldp?KN.ldp.v:44)/64, ph=(KN.lph?KN.lph.v:14)/128;
	let d=''; for(let x=0;x<=W;x+=2){ d+=(x?'L':'M')+x+' '+(H/2-lfoY(L.shape,x/W*2+ph)*dep*(H/2-8)).toFixed(1); }
	g.innerHTML=`<line class="gridl" x1="${W/2}" y1="0" x2="${W/2}" y2="${H}"/><line class="gridl" x1="0" y1="${H/2}" x2="${W}" y2="${H/2}"/><path d="${d}L${W} ${H/2}L0 ${H/2}Z" fill="${c}" fill-opacity=".42"/><path d="${d}" stroke="${c}" stroke-width="1.5" stroke-opacity=".8" fill="none"/><circle cx="0" cy="${H/2-lfoY(L.shape,ph)*dep*(H/2-8)}" r="4" fill="${c}"/>`;
}
$$('#lfotabs button').forEach(b=>b.addEventListener('click',()=>{state.cur=+b.dataset.lfo; buildShapes(); applySync(); drawLfo();}));

// ---------------- FX graphs ----------------
function drawFx(){
	{ const g=$('#g-delay'), [W,H]=fit(g,300,120), fb=(KN.dfb?KN.dfb.v:32)/127, n=10; let r='';
		for(let i=0;i<n;i++){ const x=8+i*(W-16)/n, h=(H-20)*Math.pow(0.35+fb*0.6,i); r+=`<rect x="${x}" y="${H-4-h}" width="6" height="${h}" fill="${col('k')}" opacity="${i%2?0.6:1}"/>`; }
		g.innerHTML=r; }
	{ const g=$('#g-rev'), [W,H]=fit(g,300,120); let d=`M0 ${H}`; for(let x=0;x<=W;x+=2){ d+=`L${x} ${(H-(H-12)*Math.exp(-x/(W/3))*(x<12?x/12:1)*(0.8+0.2*Math.sin(x*1.7))).toFixed(1)}`; }
		g.innerHTML=`<path d="${d}L${W} ${H}Z" fill="${col('b')}" fill-opacity=".42"/><path d="${d}" stroke="${col('b')}" fill="none" stroke-width="1.5"/>`; }
	{ const g=$('#g-cho'), [W,H]=fit(g,300,120); let a='',b=''; for(let x=0;x<=W;x+=2){ a+=(x?'L':'M')+x+' '+(H/2-H/4*Math.sin(x/40)).toFixed(1); b+=(x?'L':'M')+x+' '+(H/2-H/4*Math.sin(x/40+2)).toFixed(1); }
		g.innerHTML=`<path d="${a}" stroke="${col('g')}" fill="none" stroke-width="1.5"/><path d="${b}" stroke="${col('g')}" fill="none" stroke-width="1.5" opacity=".5"/>`; }
	{ const g=$('#g-ph'), [W,H]=fit(g,300,120), fl=$('#phmode .on').dataset.m==='fl'; let p='';
		for(let x=0;x<=W;x+=2){ const f=x/W; let m=1; if(fl) m=Math.abs(Math.cos(f*Math.PI*7))*0.85+0.15; else for(let k=1;k<=3;k++) m*=Math.abs(Math.sin(f*Math.PI*(k*1.6)+0.4))*0.6+0.4; p+=(x?'L':'M')+x+' '+(H-6-(H-12)*m).toFixed(1); }
		g.innerHTML=`<path d="${p}L${W} ${H}L0 ${H}Z" fill="${col('p')}" fill-opacity=".42"/><path d="${p}" stroke="${col('p')}" fill="none" stroke-width="1.5"/>`; }
	drawOd();
}
function drawOd(){ const g=$('#g-od'), [W,H]=fit(g,300,120), dr=(KN.od?KN.od.v:34)/127*8+0.5; let d=''; for(let x=0;x<=W;x+=3){ const t=x/W*2-1; d+=(x?'L':'M')+x+' '+(H/2-(H/2-8)*Math.tanh(t*dr)/Math.tanh(dr)).toFixed(1); }
	g.innerHTML=`<line class="gridl" x1="${W/2}" y1="0" x2="${W/2}" y2="${H}"/><line class="gridl" x1="0" y1="${H/2}" x2="${W}" y2="${H/2}"/><path d="M0 ${H-8}L${W} 8" stroke="#ffffff22" stroke-dasharray="3 3"/><path d="${d}" stroke="${col('s')}" fill="none" stroke-width="1.5"/>`; }
// phaser / flanger: the Octatrack's two pages
const PHN={ph:[['Center','CNTR'],['Depth','DEP'],['Speed','SPD'],['Feedback','FB'],['Width','WID'],['Mix','MIX'],['Stages','NUM']],
	fl:[['Delay','DEL'],['Depth','DEP'],['Speed','SPD'],['Feedback','FB'],['Width','WID'],['Mix','MIX'],null]};
$('#phmode').addEventListener('click',e=>{ const b=e.target.closest('button'); if(!b) return; setTimeout(()=>{ const ks=$$('#fx-ph .kgrid .k');
	PHN[b.dataset.m].forEach((n,i)=>{ const k=ks[i]; if(!n){ k.style.visibility='hidden'; return; } k.style.visibility=''; k.dataset.l=n[0]; k.title=n[1]; k.dataset.v=k.v; makeKnob(k); }); drawFx(); harmonize(); },0); });

// ---------------- builders for other pages ----------------
function buildMisc(){
	// output routing
	const OUTS=['Main',...[...Array(15)].map((_,i)=>'Out '+String(i+1).padStart(2,'0'))];
	$('#outsel').innerHTML=OUTS.map(o=>`<option>${o}</option>`).join('');
	// sends matrix
	const FXN=[['Delay','k'],['Reverb','b'],['Chorus','g'],['Ph/Fl','p']];
	let m='<div class="h"></div>'+TRK.map(([f,mm],i)=>`<div class="h">${short(mm)} ${i+1}</div>`).join('');
	FXN.forEach(([n,c],r)=>{ m+=`<div class="rl" style="color:var(--${c}-t)">${n}</div>`; TRK.forEach((_,i)=>{ const v=Math.round(((i*37+r*53)%127)*(((i+r)%3)?1:0)); m+=`<div class="cell" style="--cc:var(--${c})"><b style="width:${v/127*100}%"></b><span>${v}</span></div>`; }); });
	$('#matrix').innerHTML=m;
	// master mixer: fader, pan, mute, solo, out, meter per channel
	const outOf=i=>i===0?'Out 01':i===1?'Out 02':i===8?'Out 03':'Main';
	$('#chs').innerHTML=TRK.map(([f,mm],i)=>`<div class="cs"><span class="nm">${short(mm)} ${i+1}</span><span class="mn" title="${mm}">${mm}</span><i class="k nol" data-l="Pan ${i+1}" data-v="${[0,-10,12,0,-20,8,0,30,-30,20,0,-5,15,-15,0,0][i]}" data-min="-64" data-max="63" data-c="w" data-sz="52" style="--kw:40px" data-f="pan"></i><div class="ms"><button class="m${i===6?' on':''}">M</button><button class="s">S</button></div><div class="meter"><div class="vm"><b style="height:${20+i*37%60}%"></b></div><div class="vm"><b style="height:${18+i*31%60}%"></b></div><div class="f" data-v="${[110,100,90,96,100,104,80,90,70,74,84,88,92,100,60,100][i]}" style="width:28px"><div class="tr"></div><span class="v"></span></div></div><select>${OUTS.map(o=>`<option${o===outOf(i)?' selected':''}>${o}</option>`).join('')}</select></div>`).join('');
	$$('.cs .ms button').forEach(b=>b.onclick=()=>b.classList.toggle('on'));
	// seq
	$('#pat').innerHTML=[...Array(16)].map((_,i)=>`<button class="${i===0?'on':''}${i===1?' q':''}">A${String(i+1).padStart(2,'0')}</button>`).join('');
	$$('#pat button').forEach(b=>b.onclick=()=>{$$('#pat button').forEach(x=>x.classList.remove('on'));b.classList.add('on');});
	$('#trksel').innerHTML=TRK.map(([f,mm],i)=>`<button class="${i===state.track?'on':''}" data-t="${i}">${short(mm)} ${i+1}</button>`).join('');
	$('#trksel').addEventListener('click',e=>{ const b=e.target.closest('button'); if(!b) return; const i=+b.dataset.t; selectTrack(i);
		$$('.tt').forEach(x=>x.classList.toggle('on',x.textContent==='Seq')); buildParams(); });
	buildSteps();
	// kit bank: 64 slots
	const names=KITN;
	$('#slots64').innerHTML=[...Array(64)].map((_,i)=>{ const n=names[i]; return `<button class="${i===6?'on':''}${n?'':' e'}" title="${n?'':'Empty slot'}"><span class="num">${String(i+1).padStart(2,'0')}</span> ${n||''}${i===6?'<span class="mod" title="Changed since saved">&#9679;</span>':''}</button>`; }).join('');
	$$('#slots64 button').forEach(b=>b.onclick=()=>{$$('#slots64 button').forEach(x=>x.classList.remove('on'));b.classList.add('on');});
	// per-kit summary: one row per track, read from each track's chain
	const drv=c=>{ const ids=[0,1].filter(i=>c.s[i].t); return ids.length?ids.map(i=>DRIVES[c.s[i].t][2]).join(', '):'—'; };
	$('#ktab').innerHTML='<tr><th>#</th><th>Track</th><th>Family</th><th>Machine</th><th>Filter</th><th>Drive</th><th>Choke</th><th>Out</th></tr>'+
		TRK.map(([f,mm],i)=>`<tr><td class="mut">${i+1}</td><td>${SNAMES[i]?'<b>'+SNAMES[i]+'</b>':'—'}</td><td class="fm">${FAM.find(x=>x.id===f).n}${f==='an'?' <span class="plus">+</span>':''}</td><td>${mm}</td><td>${FTL(FTYPES[CH[i].ftype][0])}</td><td class="mut">${drv(CH[i])}</td><td class="mut">${[...CHOKE[i]].map(t=>t+1).join(', ')||'—'}</td><td>${outOf(i)}</td></tr>`).join('');
	buildBrowser();
}
let STEPS=[...Array(16)].map((_,i)=>({trig:[0,4,8,10,12,14].includes(i), cond:i===4?'1:2':(i===14?'FILL':''), rt:i===12?3:1, mt:i===4?5:(i===10?-6:0), acc:[0,8,12].includes(i), sld:i===10, vel:[100,0,0,0,84,0,0,0,100,0,56,0,100,0,96,0][i]}));
// locks: step -> {box|label: raw knob value}; ranges per key for the lanes
let LOCKS={4:{'b-machine|Tune':7,'b-sample|Start':14,'b-filter|Frequency':96}, 8:{'b-machine|Decay':100}, 10:{'b-filter|Frequency':40}, 14:{'b-machine|Decay':110}, 12:{'b-filter|Frequency':70}};
const LRANGE={'b-machine|Tune':[-24,24],'b-filter|Resonance':[0,127],'b-sample|Start':[0,127],'b-filter|Frequency':[0,127],'b-machine|Decay':[0,127]};
let LANES=['b-filter|Frequency','b-machine|Tune','b-machine|Decay','b-sample|Start','b-filter|Resonance'];
LOCKS[2]={'b-filter|Resonance':90}; LOCKS[6]={'b-filter|Resonance':60};
Object.entries(LOCKS).forEach(([i,l])=>{ const ks=Object.keys(l); if(ks.length) STEPS[i].lk=ks[ks.length-1]; });
const BOXN={'b-machine':'Machine','b-sample':'Sample','b-filter':'Filter','b-amp':'Amp','b-drvA':'Drive A','b-drvB':'Drive B','b-band':'Band','b-lfo1':'LFO 1','b-lfo2':'LFO 2','b-mixer':'Sends'};
const DISP={};	// key -> name as the PARAMETERS list shows it
const lkey=k=>(k.closest('[data-box]')?k.closest('[data-box]').dataset.box:k.closest('.box').id)+'|'+k.dataset.l;
const lname=k=>DISP[k]||((BOXN[k.split('|')[0]]||'')+' '+k.split('|')[1]);
let SEL=new Set([4,8]);
// each step remembers the parameter last locked on it (st.lk); ACTK is the knob last turned on the track page
let ACTK='b-filter|Frequency', stepDragged=false;
const velF=s=>s.acc?1:(s.vel||100)/127;
// what a vertical drag on step i edits in the current mode: Vel mode its velocity (any trig), Trig mode its last locked
// parameter (a selected trig or trigless lock holding that lock); null: the step does not drag
function stepVal(i){ const s=STEPS[i];
	if(stepMode==='vel') return s.trig?{lock:false, v:s.vel||100, f:velF(s)}:null;
	if(stepMode!=='trig'||!(s.trig||s.tl)||!SEL.has(i)) return null;
	const v=(LOCKS[i]||{})[s.lk]; if(v==null) return null;
	const [mn,mx]=LRANGE[s.lk]||[0,127]; return {lock:true, k:s.lk, v, f:(v-mn)/(mx-mn)}; }
function stepShown(i,sv){ if(!sv.lock) return sv.v; const k=$$('.tp .k').find(k=>lkey(k)===sv.k); return k?fmt(k,sv.v):sv.v; }
function paintLockPanel(){ const held={}; SEL.forEach(i=>Object.entries(LOCKS[i]||{}).forEach(([k,v])=>held[k]=v));
	const hi=SEL.has(selStep)?STEPS[selStep].lk:null;
	const rows=Object.entries(held).sort(([a],[b])=>(b===hi)-(a===hi)).slice(0,4);
	$$('.lockpanel').forEach(lp=>lp.innerHTML = rows.map(([k,v])=>`<div class="lockrow${k===hi?' act':''}"><span>${lname(k)}</span><b>${v}</b></div>`).join('')); }
let selStep=4, newLane=null;
// step cells carry everything on the 16 columns: number, condition, ratchet, micro-timing, lock dot, accent bar (top), slide link (to the next step)
let stepMode='trig';
function buildSteps(){
	const html=STEPS.map((s,i)=>{ const n=Object.keys(LOCKS[i]||{}).length;
		const tip=[s.trig?'Trig':(s.tl?'Trigless lock':'No trig'),s.acc?'accent':'',s.sld?'slide to the next step':'',n?n+' lock'+(n>1?'s':''):''].filter(Boolean).join(', ');
		return `<div class="st${s.trig?' trig':''}${!s.trig&&s.tl?' tl':''}${s.trig&&s.acc?' acc':''}${n?' lk':''}${SEL.has(i)?' sel':''}${stepVal(i)?' dg':''}" data-i="${i}" title="${tip}" style="--vh:${stepMode==='vel'?Math.round(velF(s)*100):100}%${(sv=>sv&&sv.lock?';--lh:'+Math.round(sv.f*100)+'%':'')(stepVal(i))}"><b class="dv"></b>${s.sld?'<i class="sld"></i>':''}<span class="no">${i+1}</span>${n?'<i class="lkdot"></i>':''}<span class="cd">${s.cond}</span><div class="rt">${s.rt>1?[...Array(s.rt)].map(()=>'<i></i>').join(''):''}</div>${s.mt?`<i class="mt" style="left:50%;width:${Math.abs(s.mt)*2}px;${s.mt<0?'transform:translateX(-100%)':''}"></i>`:''}</div>`; }).join('');
	$$('.steps').forEach(c=>c.innerHTML=html);
	$$('.st').forEach(el=>{
		el.onpointerdown=ev=>{ const i=+el.dataset.i, s=STEPS[i], sv=stepVal(i); stepDragged=false; if(ev.button||!sv) return;
			const y0=ev.clientY, h=el.getBoundingClientRect().height; let moved=false; el.setPointerCapture(ev.pointerId);
			el.onpointermove=e=>{ if(!moved&&Math.abs(e.clientY-y0)<4) return;
				if(!moved){ moved=true; el.classList.add('drag'); if(sv.lock){ selStep=i; paintLockPanel(); } }
				const f=Math.max(0,Math.min(1,sv.f+(y0-e.clientY)/h));
				if(sv.lock){ const [mn,mx]=LRANGE[sv.k]||[0,127]; LOCKS[i][sv.k]=Math.round(mn+f*(mx-mn)); }
				else { s.vel=Math.max(1,Math.round(f*127)); s.acc=false; }
				const nv=stepVal(i); el.style.setProperty('--vh',Math.round(nv.f*100)+'%'); el.querySelector('.dv').textContent=stepShown(i,nv); paintLockPanel(); };
			el.onpointerup=()=>{ el.onpointermove=null; el.onpointerup=null; if(moved){ stepDragged=true; setTimeout(()=>{ buildSteps(); },0); } }; };
		el.onclick=ev=>{ if(stepDragged){ stepDragged=false; return; } const i=+el.dataset.i, mode=ev.altKey?'acc':(ev.shiftKey?'sld':stepMode);
			if(mode==='acc'||mode==='sld'){ STEPS[i][mode]=!STEPS[i][mode]; buildSteps(); return; }
			if(ev.ctrlKey||ev.metaKey){ SEL.has(i)?SEL.delete(i):SEL.add(i); }
			else { if(SEL.has(i)&&SEL.size===1) SEL.clear(); else { SEL.clear(); SEL.add(i); } }
			if(SEL.size) selStep=[...SEL][0]; buildSteps(); };
		el.ondblclick=ev=>{ if(stepMode!=='trig'||ev.altKey||ev.shiftKey) return; const i=+el.dataset.i; STEPS[i].trig=!STEPS[i].trig; buildSteps(); };
	});
	// knobs holding a lock on the selection get the lock ring
	const held={}; SEL.forEach(i=>Object.entries(LOCKS[i]||{}).forEach(([k,v])=>held[k]=v));
	$$('.tp .k').forEach(k=>{ const key=lkey(k), on=key in held; k.classList.toggle('lk', on); if(!k.set) return;
		if(on){ if(k.baseV==null) k.baseV=k.v; if(k.v!==held[key]) k.set(held[key]); }
		else if(k.baseV!=null){ const b=k.baseV; k.baseV=null; k.set(b); } });
	paintLockPanel();
	const s=STEPS[selStep]; $('#sd-title').textContent=selStep+1;
	$('#mtv').textContent=(s.mt>=0?'+':'')+s.mt+'/384'; $('#mtcap').style.left=`calc(${50+s.mt/23*48}% - 3px)`;
	$$('#ratchet button').forEach((b,i)=>b.classList.toggle('on',i+1===s.rt));
	buildLanes(); if($('#p-seq').classList.contains('on')) buildParams();
}
document.addEventListener('click',e=>{ const b=e.target.closest('.stmode button'); if(!b) return; stepMode=b.dataset.sm; $$('.stmode button').forEach(x=>x.classList.toggle('on',x.dataset.sm===stepMode)); buildSteps(); });
function buildLanes(){
	const P=$('#plocks'); if(P) P.innerHTML=Object.keys(LOCKS).sort((x,y)=>x-y).filter(i=>Object.keys(LOCKS[i]).length).map(i=>`<div class="lockrow" style="margin-bottom:3px"><span style="width:60px;color:var(--mut)">Step ${+i+1}</span><span style="flex:1">${Object.entries(LOCKS[i]).map(([k,v])=>lname(k)+' <b>'+v+'</b>').join(', ')}</span></div>`).join('');
	const L=$('#lanes'); if(!L) return;
	L.innerHTML=LANES.map(k=>{ const [mn,mx]=LRANGE[k]||[0,127];
		return `<div class="lane${k===newLane?' new':''}" data-k="${k}"><div class="ln">${lname(k)}<small>${mn}..${mx}</small></div><div class="cells">${STEPS.map((_,i)=>{ const v=(LOCKS[i]||{})[k]; return `<i data-i="${i}">${v==null?'':`<b style="height:${(v-mn)/(mx-mn)*100}%"></b>`}</i>`; }).join('')}</div></div>`; }).join('');
	L.querySelectorAll('.lane').forEach(lane=>{ const k=lane.dataset.k, cells=lane.querySelector('.cells'), [mn,mx]=LRANGE[k]||[0,127];
		cells.addEventListener('pointerdown',e=>{ const c0=e.target.closest('i'); if(!c0) return;
			if(e.altKey||$('#b-lanes .tabs .on').textContent==='Erase'){ const i=+c0.dataset.i; if(LOCKS[i]) delete LOCKS[i][k]; buildSteps(); return; }
			cells.setPointerCapture(e.pointerId);
			const paint=ev=>{ const r=cells.getBoundingClientRect(), i=Math.max(0,Math.min(15,Math.floor((ev.clientX-r.left)/r.width*16))), f=1-(ev.clientY-r.top)/r.height;
				(LOCKS[i]=LOCKS[i]||{})[k]=Math.round(mn+Math.max(0,Math.min(1,f))*(mx-mn));
				const cell=cells.children[i]; cell.innerHTML=`<b style="height:${(LOCKS[i][k]-mn)/(mx-mn)*100}%"></b>`; };
			paint(e); cells.onpointermove=paint; cells.onpointerup=()=>{ cells.onpointermove=null; buildSteps(); }; }); });
}
// SEQ: the track's lockable parameters, grouped by box; a click adds or removes the parameter's lane
const LFO_OTHER={'Speed':'16','Multiplier':'2','Fade In/Out':'0','Start Phase':'32','Depth':'20'};
function knobItems(ks){ return ks.filter(k=>k&&!k.classList.contains('empty')&&k.style.display!=='none').map(k=>({k:lkey(k),
	n:k.closest('.drv')?(k.dataset.dl||k.dataset.l)+(k.dataset.plus?' +':''):(k.dataset.dl?k.dataset.l:k.querySelector('.l').textContent),
	v:k.querySelector('text').textContent, r:[+(k.dataset.min??0),+(k.dataset.max??127)]})); }
function lockables(){
	const q=s=>$$(s), G=[];
	const add=(n,c,it)=>{ G.push({n:it.length?n:n+' (off)',c,it}); it.forEach(x=>{ DISP[x.k]=sc(n+' '+x.n); if(!LRANGE[x.k]) LRANGE[x.k]=x.r; }); };
	add('Machine','t',knobItems(q('#mknobs .k')));
	add('Sample','g',knobItems(q('#s-main .k')));
	add('Drive A','a',knobItems(q('[data-box="b-drvA"] .k')));
	add('Drive B','a',knobItems(q('[data-box="b-drvB"] .k')));
	add('Band','b',knobItems(q('#b-band .pane.on .k')));
	add('Filter','s',knobItems(q('#b-filter .k')));
	add('Amp','p',knobItems(q(state.fam==='md'?'#b-amp .kr:not(.ampenv) .k':'#b-amp .k')));
	const lk=knobItems(q('#b-mod .k')); [0,1].forEach(l=>add('LFO '+(l+1),'y',lk.map(x=>{ const nm=x.k.split('|')[1]; return {...x,k:'b-lfo'+(l+1)+'|'+nm,v:l===state.cur?x.v:(LFO_OTHER[nm]??x.v)}; })));
	add('Sends','w',q('#b-mixer .f').map(f=>({k:'b-mixer|'+f.dataset.l,n:f.dataset.l,v:String(f.v),r:[0,127]})));
	return G; }
function buildParams(){
	const P=$('#plist'); if(!P) return; const G=lockables(), held=new Set(); Object.values(LOCKS).forEach(o=>Object.keys(o).forEach(k=>held.add(k)));
	P.innerHTML=G.map(g=>`<div class="pgp" style="--gc:var(--${g.c})"><div class="ph">${g.n}</div>${g.it.map(x=>`<button class="pr${LANES.includes(x.k)?' on':''}${held.has(x.k)?' locked':''}" data-k="${x.k}" title="${LANES.includes(x.k)?'Remove its lane':'Add a lock lane'}"><span>${sc(x.n)}</span><b>${x.v}</b></button>`).join('')}</div>`).join('');
	P.querySelectorAll('.pr').forEach(b=>b.onclick=()=>toggleLane(b.dataset.k));
	$$('#trksel button').forEach(b=>b.classList.toggle('on',+b.dataset.t===state.track));
	buildLanes();
}
function toggleLane(k){ const i=LANES.indexOf(k); if(i>=0){ LANES.splice(i,1); newLane=null; } else { if(LANES.length>=8) LANES.shift(); LANES.push(k); newLane=k; } buildLanes(); buildParams(); }
$$('.lockrec').forEach(b=>b.onclick=()=>{ const on=!b.classList.contains('on'); $$('.lockrec').forEach(x=>x.classList.toggle('on',on)); });
document.addEventListener('keydown',e=>{ if(e.key==='Escape'&&SEL.size){ SEL.clear(); buildSteps(); } });
document.querySelector('.main').addEventListener('click',e=>{ if(!SEL.size||e.target.closest('button,select,.k,.f,.st,svg,.slot,.mini,.lane,.hd')) return; SEL.clear(); buildSteps(); });
$('#ratchet').addEventListener('click',e=>{ const b=e.target.closest('button'); if(!b) return; STEPS[selStep].rt=+b.textContent; buildSteps(); });
$('#mtbar').addEventListener('pointerdown',e=>{ const bar=$('#mtbar'), r=bar.getBoundingClientRect(); const set=ev=>{ STEPS[selStep].mt=Math.max(-23,Math.min(23,Math.round(((ev.clientX-r.left)/r.width-0.5)*46))); buildSteps(); }; set(e); bar.setPointerCapture(e.pointerId); bar.onpointermove=set; bar.onpointerup=()=>bar.onpointermove=null; });
$('#cond').onchange=e=>{ STEPS[selStep].cond=e.target.value==='None'?'':e.target.value; buildSteps(); };
$('#scale').addEventListener('click',()=>{});


// ---------------- modulation: LFO 1, LFO 2 and Velocity (LFOs: ASSIGN latched only; Velocity: the knob's '+'; ring on the target) ----------------
const LFOC=[col('y'), col('m'), col('v')];
let MODS={'b-machine|Decay':{lfo:0,depth:22},'b-amp|Level':{lfo:2,depth:30}};	// velocity drives Level by default
let assignArmed=-1;
function modKey(el){ try{ return lkey(el); }catch(e){ return null; } }
function applyMods(){ $$('.k').forEach(k=>k.drawMod&&k.drawMod()); updateDest(); }
function buildDest(){ const sel=$('#ldest'); if(!sel) return; const G=lockables().filter(g=>!/^LFO/.test(g.n)&&g.it.length);
	sel.innerHTML='<option value="">None</option>'+G.map(g=>`<optgroup label="${g.n}">${g.it.map(x=>`<option value="${x.k}">${sc(g.n+" "+x.n)}</option>`).join('')}</optgroup>`).join(''); updateDest(); }
function updateDest(){ const sel=$('#ldest'); if(!sel) return; const e=Object.entries(MODS).find(([k,m])=>m.lfo===state.cur); sel.value=e?e[0]:''; if(sel.selectedIndex<0) sel.value=''; }
function setMod(el,lfo){ const k=modKey(el); if(!k) return; Object.keys(MODS).forEach(x=>{ if(MODS[x].lfo===lfo&&!(lfo===2&&x==='b-amp|Level')) delete MODS[x]; }); MODS[k]={lfo,depth:24}; applyMods(); el.classList.add('flashk'); setTimeout(()=>el.classList.remove('flashk'),600); }
function arm(lfo){ assignArmed=lfo; $$('.assign').forEach(b=>{ b.classList.toggle('on',lfo>=0); b.title=lfo>=0?'Armed: touch a knob (Esc cancels)':'Assign this LFO: touch a knob after'; }); }
document.addEventListener('pointerdown',e=>{ if(assignArmed<0) return; const k=e.target.closest('.tp .k'); if(!k||k.closest('#b-mod')) return; setMod(k,assignArmed); arm(-1); e.stopPropagation(); },true);
document.addEventListener('keydown',e=>{ if(e.key==='Escape') arm(-1); });
$$('.assign').forEach(b=>b.onclick=()=>arm(assignArmed>=0?-1:state.cur));
$('#ldest').onchange=e=>{ const k=e.target.value; Object.keys(MODS).forEach(x=>{ if(MODS[x].lfo===state.cur) delete MODS[x]; }); if(k) MODS[k]={lfo:state.cur,depth:24}; applyMods(); };
$$('#lfotabs button').forEach(b=>b.addEventListener('click',()=>{ $('#lfobd').dataset.box='b-lfo'+(state.cur+1); if(assignArmed>=0) arm(state.cur); applyMods(); }));
function clearGhost(){}

// ---------------- knob popover: '+' badge or right-click -> Velocity / lock (LFOs are assigned by ASSIGN alone) ----------------
function closePop(){ $$('.kpop').forEach(p=>p.remove()); $$('.k.popk').forEach(k=>k.classList.remove('popk')); }
function openPop(el){
	closePop(); const win=$('.win').getBoundingClientRect(), r=el.getBoundingClientRect(), k=modKey(el);
	const sl=[...SEL].sort((a,b)=>a-b).map(i=>i+1);
	const p=document.createElement('div'); p.className='kpop';
	p.innerHTML=`
		<button data-a="2"><i style="background:${LFOC[2]}"></i>Velocity</button>
		<button data-a="lock" ${sl.length?'':'disabled'}><i style="background:#fff"></i>${sl.length?'Lock on step'+(sl.length>1?'s ':' ')+sl.join(', '):'Lock'}</button>`;
	if(!sl.length) p.querySelector('[data-a="lock"]').title='Select steps first';
	p.style.left=Math.min(r.right-win.left-6,1600-230)+'px'; p.style.top=(r.top-win.top+14)+'px';
	p.addEventListener('pointerdown',e=>e.stopPropagation());
	p.onclick=e=>{ const b=e.target.closest('button'); if(!b) return; const a=b.dataset.a;
		if(a==='2') setMod(el,2);
		if(a==='lock'){ LRANGE[k]=[+(el.dataset.min??0),+(el.dataset.max??127)]; SEL.forEach(i=>{ (LOCKS[i]=LOCKS[i]||{})[k]=el.v; }); buildSteps(); }
		closePop(); };
	$('.win').appendChild(p); el.classList.add('popk');
}
document.addEventListener('pointerdown',e=>{ if(!e.target.closest('.kpop')&&!e.target.closest('.pb')) closePop(); });
document.addEventListener('contextmenu',e=>{ const k=e.target.closest('.tp .k'); if(!k) return; e.preventDefault(); openPop(k); });
document.addEventListener('keydown',e=>{ if(e.key==='Escape'){ closePop(); collapse(); $('#famtoast').classList.remove('on'); } });

// no box expansion: every box keeps its place and size
function collapse(){}

// ---------------- footer zoom ----------------
$('#zoomsel').onchange=e=>{ $('.win').style.zoom=parseInt(e.target.value)/100; };

const LED=Array(16).fill(0); LED[0]=1; LED[1]=0.55; LED[8]=0.25;
function hitTrack(i){ LED[i]=1; drawLeds(); }
function drawLeds(){ $$('.tt[data-t]').forEach(b=>b.style.setProperty('--lv',LED[+b.dataset.t].toFixed(2))); }
// ---------------- sound browser (ticket 21, Q12-Q14) ----------------
const BTYPES=['Kick','Snare','Clap','Closed hat','Open hat','Cymbal','Tom','Perc','Chord','Tonal','FX','Loop','Other'];
const BTAGS=['Acoustic','Electronic','Dry','Processed','Lo-fi','Short','Long'];
// [name, type, guessed by ('name' | 'audio' | '' = set by hand), tags, length, origin, tree node]
const KITN=['TRX UW','EFM heavy','Hybrid 1','Dry run','Strange','MC tones','Rainy day','DT2 breaks','FM kit','Night shift','Room','Analog?','Dub','808ish','Techno 1','Techno 2','Swarm pads','Bits n bobs','Clap stack','Twinshot 1'];
const BITEMS0=[
	['Winston kick','Kick','',['Electronic','Short'],'','Syntakt BD MODERN','snd-mine'],['Dry snare','Snare','',['Dry'],'','Syntakt SD BASIC','snd-mine'],
	['TRX tom','Tom','name',['Electronic'],'','Machinedrum TRX-XT','snd-kits'],['Cowbell','Perc','name',['Electronic','Short'],'','Machinedrum TRX-CB','snd-kits'],
	['kick_round_04.wav','Kick','name',['Acoustic','Dry','Short'],'0.42 s','My samples / Kicks','my-kicks'],['kick_sub_long.wav','Kick','name',['Electronic','Long'],'1.80 s','My samples / Kicks','my-kicks'],
	['snr_brush_2.wav','Snare','name',['Acoustic'],'0.61 s','My samples / Snares','my-snares'],['take 12.wav','Perc','audio',['Acoustic'],'0.35 s','My samples / Field recordings','my-field'],
	['door slam.wav','FX','audio',['Acoustic','Processed'],'1.12 s','My samples / Field recordings','my-field'],['loop_92_dusty.wav','Loop','name',['Lo-fi','Long'],'2.61 s','My samples / Loops','my-loops'],
	['ROM-01','Kick','audio',['Electronic','Lo-fi'],'0.30 s','Machinedrum UW bank','el-md'],['ROM-02','Snare','audio',['Lo-fi'],'0.28 s','Machinedrum UW bank','el-md'],
	['ROM-07','Closed hat','audio',['Lo-fi','Short'],'0.09 s','Machinedrum UW bank','el-md'],['ROM-12','Chord','audio',['Lo-fi','Long'],'1.46 s','Machinedrum UW bank','el-md'],
	['Twinshot 03','Clap','audio',['Electronic'],'0.25 s','Syntakt firmware','el-st'],['BD Plastic 4','Kick','name',['Electronic'],'0.51 s','Analog Rytm factory pack','el-ar'],
	['CY Ride 1','Cymbal','name',['Acoustic','Long'],'2.20 s','Analog Rytm factory pack','el-ar'],['OH Classic 2','Open hat','name',['Electronic'],'0.74 s','Analog Rytm factory pack','el-ar'],
	['HOUSE_TONE_C','Tonal','name',['Electronic'],'0.90 s','House','house'],['HOUSE_NOISE_SWEEP','FX','name',['Processed','Long'],'1.70 s','House','house'],
	['set2 / tom lo','Tom','name',['Acoustic'],'0.88 s','Transfer export set2','el-tr'],['set2 / chh','Closed hat','name',['Acoustic','Short'],'0.07 s','Transfer export set2','el-tr'],
];
const BTREE=[	// [id, label, level, header]
	['kits','Kits',0,1],['snd','Track sounds',0,1],['snd-mine','My sounds',1],['snd-kits','From kits',1],
	['smp','Samples',0,1],['house','House',1],['my','My samples',1],['my-kicks','Kicks',2],['my-snares','Snares',2],['my-loops','Loops',2],['my-field','Field recordings',2],
	['el','Elektron',1],['el-st','Syntakt',2],['el-md','Machinedrum',2],['el-ar','Analog Rytm',2],['el-dt','Digitakt II',2],['el-tr','Transfer exports',2]];
const FOUND=[['Syntakt OS 1.41','Firmware','ok','TWINSHOT samples'],['Machinedrum flash image','Firmware','ok','UW bank: ROM-01 to -32'],
	['Analog Rytm factory pack','Factory pack','ok',''],['Digitakt II factory pack','Factory pack','miss',''],['Transfer exports','2 folders','ok','set1, set2']];
// lengths in seconds, without the unit (the column says Length)
const BITEMS=BITEMS0.map(x=>{ const y=[...x]; y[4]=y[4].replace(/ s$/,''); return y; });
BITEMS.unshift(...KITN.map((n,i)=>[n,'Kit','',[],'','Bank slot '+String(i+1).padStart(2,'0'),'kits']));
const B0=BITEMS.findIndex(x=>x[0]==='kick_round_04.wav');
let bView='src', bNode='smp', bSel=new Set([B0]), bPlay=B0, bTags=new Set(), bQ='';
const under=(node,id)=>id===node||id.startsWith(node+'-')||(node==='snd'&&id.startsWith('snd-'))||(node==='smp'&&!id.startsWith('snd')&&id!=='kits');
function bVisible(){ return BITEMS.map((x,i)=>[x,i]).filter(([x])=>(bView==='type'?(bNode==='all'||x[1]===bNode):under(bNode,x[6]))
	&&(!bQ||x[0].toLowerCase().includes(bQ))&&[...bTags].every(t=>x[3].includes(t))); }
function buildBrowser(){
	$('#efound').innerHTML=FOUND.map(([n,k,st,d])=>`<div class="fd${st==='miss'?' missing':''}" title="${st==='miss'?'Not found: download it from elektron.com, then drop it on the Elektron folder':n+(d?': '+d:'')}"><b>${n}</b><span>${k}</span><span class="${st}">${st==='ok'?'Found':'Missing'}</span></div>`).join('');
	const cnt=f=>BITEMS.filter(f).length;
	$('#btree').innerHTML = bView==='src'
		? BTREE.map(([id,l,lv,h])=>{ const n=cnt(x=>under(id,x[6])); return `<button class="tn2 l${lv}${h?' hdr':''}${id===bNode?' on':''}" data-n="${id}"${n?'':' title="Nothing found yet"'}>${l}<em>${id==='kits'?n+' / 64':n}</em></button>`; }).join('')
		: [['all','All types']].concat(BTYPES.map(t=>[t,t])).map(([id,l])=>`<button class="tn2 l0${id==='all'?' hdr':''}${id===bNode?' on':''}" data-n="${id}">${l}<em>${id==='all'?cnt(x=>x[1]!=='Kit'):cnt(x=>x[1]===id)}</em></button>`).join('');
	$$('#btree .tn2').forEach(b=>b.onclick=()=>{ bNode=b.dataset.n; buildBrowser(); });
	$('#bchips').innerHTML=BTAGS.map(t=>`<button class="chip${bTags.has(t)?' on':''}" data-t="${t}">${t}</button>`).join('');
	$$('#bchips .chip').forEach(b=>b.onclick=()=>{ const t=b.dataset.t; bTags.has(t)?bTags.delete(t):bTags.add(t); buildBrowser(); });
	const rows=bVisible();
	$('#brows').innerHTML=rows.map(([x,i])=>`<tr class="brow${bSel.has(i)?' sel':''}${bPlay===i?' play':''}" data-i="${i}" draggable="true"><td>${x[0]}</td><td>${x[1]==='Kit'?'Kit':`<select class="${x[2]?'auto':''}" title="${x[2]==='name'?'Guessed from the name':(x[2]==='audio'?'Guessed from the audio':'Set by hand')}">${BTYPES.map(t=>`<option${t===x[1]?' selected':''}>${t}</option>`).join('')}</select>`}</td><td>${x[3].map(t=>`<span class="tag">${t}</span>`).join('')}</td><td class="mut">${x[4]}</td><td class="mut">${x[5]}</td></tr>`).join('');
	$$('#brows .brow').forEach(r=>{ const i=+r.dataset.i;
		r.onclick=e=>{ if(e.target.closest('select')) return; if(e.ctrlKey||e.metaKey){ bSel.has(i)?bSel.delete(i):bSel.add(i); } else if(e.shiftKey&&bSel.size){ const vis=rows.map(v=>v[1]), a=vis.indexOf([...bSel].pop()), b=vis.indexOf(i); vis.slice(Math.min(a,b),Math.max(a,b)+1).forEach(v=>bSel.add(v)); } else { bSel=new Set([i]); bPlay=i; } buildBrowser(); };
		r.ondblclick=()=>loadItem(i, $(`.tt[data-t="${state.track}"]`));
		r.ondragstart=e=>{ e.dataTransfer.setData('text/plain',String(i)); e.dataTransfer.effectAllowed='copy'; };
		const sel=r.querySelector('select'); if(sel) sel.onchange=()=>{ BITEMS[i][1]=sel.value; BITEMS[i][2]=''; buildBrowser(); }; });
	const n=bSel.size; $('#bulk').classList.toggle('on',n>1); $('#bcount').textContent=n+' selected';
	$('#bretype').innerHTML='<option value="">Retype</option>'+BTYPES.map(t=>`<option>${t}</option>`).join('');
	$('#bretype').onchange=e=>{ if(!e.target.value) return; bSel.forEach(i=>{ if(BITEMS[i][1]!=='Kit'){ BITEMS[i][1]=e.target.value; BITEMS[i][2]=''; } }); buildBrowser(); };
	const it=BITEMS[bPlay];
	$('#pinfo').innerHTML = it[1]==='Kit' ? `<span>Families</span><b>Syntakt, Machinedrum, Rytm</b><span>Slot</span><b>${it[5].replace('Bank slot ','')}</b>`
		: `<span>Rate</span><b>${it[5].startsWith('Machinedrum')?'44.1k':'48k'}</b><span>Bits</span><b>${it[5].startsWith('Machinedrum')?'12':'24'}</b><span>Channels</span><b>${it[3].includes('Long')?'Stereo, summed to mono':'Mono'}</b>`;
	$('#pload').textContent='Load on '+sndName(state.track);
	$('#pload').onclick=()=>loadItem(bPlay, $(`.tt[data-t="${state.track}"]`));
	drawPWave();
}
function drawPWave(){ const g=$('#g-pwave'); if(!g||!g.clientWidth) return; const [W,H]=fit(g,320,150); let sd=bPlay*977+3;
	const r=()=>{ sd=(sd*16807)%2147483647; return sd/2147483647; }, len=BITEMS[bPlay][4]?parseFloat(BITEMS[bPlay][4]):1, w=[...Array(160)].map((_,i)=>{ const t=i/160; return (0.12+0.88*Math.exp(-t*6/Math.max(.3,len)))*(0.4+0.6*r()); });
	g.innerHTML=waveSvg(W,H,col('g'),0,W,w); }
function loadItem(i,tab){ if(!tab) return; tab.classList.add('dropon'); hitTrack(+tab.dataset.t||0); setTimeout(()=>tab.classList.remove('dropon'),600); }
document.addEventListener('dragover',e=>{ const t=e.target.closest('.tt[data-t],.tt.kit,#b-sample,#slots64 button'); if(t){ e.preventDefault(); $$('.dropon').forEach(x=>x!==t&&x.classList.remove('dropon')); t.classList.add('dropon'); } });
document.addEventListener('dragleave',e=>{ const t=e.target.closest('.dropon'); if(t) t.classList.remove('dropon'); });
document.addEventListener('drop',e=>{ const t=e.target.closest('.tt[data-t],.tt.kit,#b-sample,#slots64 button'); if(!t) return; e.preventDefault(); loadItem(+e.dataTransfer.getData('text/plain'),t); });
$('#bview').addEventListener('click',e=>{ const b=e.target.closest('button'); if(!b) return; bView=b.dataset.v; bNode=bView==='src'?'smp':'all'; setTimeout(buildBrowser,0); });
$('#bq').addEventListener('input',e=>{ bQ=e.target.value.toLowerCase(); buildBrowser(); });
$('#eimport').onclick=()=>{ const d=$('#edrop'); d.classList.add('over'); setTimeout(()=>d.classList.remove('over'),700); };

// ---------------- choke: the tracks this track cuts, picked on a 16-cell grid ----------------
function showChoke(){ const v=$('#chokeval'); if(v) v.textContent=[...CHOKE[state.track]].map(t=>t+1).join(' '); }
function openChoke(){
	closeMpop(); const win=$('.win').getBoundingClientRect(), r=$('#chokebtn').getBoundingClientRect(), me=state.track;
	const p=document.createElement('div'); p.className='cpop';
	p.innerHTML=TRK.map(([f,m],i)=>`<button data-t="${i}" class="${CHOKE[me].has(i)?'on':''}" ${i===me?'disabled title="This track"':`title="${sndName(i)}"`}>${i+1}</button>`).join('');
	p.style.left=(r.left-win.left)+'px'; p.style.top=(r.bottom-win.top+4)+'px';
	p.addEventListener('pointerdown',e=>e.stopPropagation());
	p.onclick=e=>{ const b=e.target.closest('button'); if(!b||b.disabled) return; const t=+b.dataset.t; CHOKE[me].has(t)?CHOKE[me].delete(t):CHOKE[me].add(t); b.classList.toggle('on'); showChoke(); };
	$('.win').appendChild(p);
}
$('#chokebtn').onclick=e=>{ e.stopPropagation(); $('.cpop')?closeMpop():openChoke(); };
$('#mmenu').onclick=e=>{ e.stopPropagation(); $('.mpop')?closeMpop():openMpop(); };
document.addEventListener('pointerdown',e=>{ if(!e.target.closest('.mpop,.cpop,#mmenu,#chokebtn')) closeMpop(); });
document.addEventListener('keydown',e=>{ if(e.key==='Escape') closeMpop(); });
// ---------------- init ----------------
CH=TRK.map(([f])=>famChain(f));
CH[12].s[1]=slot('MD SRR','post',40); CH[12].band='dt'; CH[12].ftype=9;	// track 13: a mixed chain (DT2 band, DT2 LP4, an MD SRR after the filter)
buildTracks(); buildFams(); buildMisc();
$$('.k').forEach(makeKnob); $$('.f').forEach(makeFader);
setMachine('st','BD MODERN');
buildSteps();
['fq','rs','fw'].forEach(id=>KN[id].addEventListener('kv',drawFilter));
['fd','fa','fdc','fs','fr'].forEach(id=>KN[id].addEventListener('kv',drawFenv));
hookEnv(); setAmpMode('adsr');
drawWave(); ['ss','se','sl'].forEach(id=>KN[id].addEventListener('kv',drawWave));
buildShapes(); applySync(); drawLfo(); ['ldp','lph'].forEach(id=>KN[id].addEventListener('kv',drawLfo));
drawFx(); KN.od.addEventListener('kv',drawOd); ['dtm','dfb'].forEach(id=>KN[id].addEventListener('kv',drawFx));
['bb','bw','eqf','eqg','lf','lg','mf','mg','hf','hg'].forEach(id=>KN[id].addEventListener('kv',drawBand));
show('track');
buildDest(); applyMods();
// value sizes are measured in the loaded face: measure again once the fonts are in
document.fonts.ready.then(()=>harmonize());
// second state of the track page: a family change over a changed chain asks first
function demoFamily(){ closePop(); clearGhost(); $$('.mtab')[0].click(); $('.tt[data-t="0"]').click(); if(!chainModified(state.fam)){ C().band='3b'; applyChain(); }
	const to=state.fam==='md'?['st','BD MODERN']:['md','TRX-BD']; askFamily(to[0],to[1]); }
function demoKnob(){ $$('.mtab')[0].click(); $('.tt[data-t="0"]').click(); setTimeout(()=>{ openPop($$('#mknobs .k')[2]); },50); }
// mockup states: v12.html?state=family (the family-change prompt) or ?state=knob (the knob menu)
{ const st=new URLSearchParams(location.search).get('state'); if(st==='family') demoFamily(); if(st==='knob') demoKnob(); }
KN.len.addEventListener('kv',e=>{ const n=Math.ceil(e.detail/16); $$('#pgs i').forEach((p,i)=>{p.className=i===0?'on':(i<n?'':'dis');}); });
// master overdrive: DT2 (digital, firmware) or the Syntakt's analog stage, modelled ("approx", +)
const ODN={dt:['Master Overdrive','MOVD','Digitakt II master, in the firmware\'s fixed order: compressor, this overdrive (MOVD, the same saturator as the track overdrive), then clip. Main only. The curve is drawn, not measured.'],
	st:['Overdrive','OVERDRIVE','Syntakt analog overdrive: a circuit model tuned by ear, not firmware (+). The Syntakt has no digital master.']};
function setOd(n){ const [l,t,txt]=ODN[n]; KN.od.dataset.l=l; KN.od.title=t; KN.od.dataset.v=KN.od.v; makeKnob(KN.od); KN.od.addEventListener("kv",drawOd); drawOd(); harmonize(); }
$('#odsrc').addEventListener('click',e=>{ const b=e.target.closest('button'); if(b&&!b.classList.contains('dis')) setOd(b.dataset.n); else if(b){ b.classList.remove('on'); $('#odsrc button').classList.add('on'); } });
setOd('dt');
// live meters
let tick=0; setInterval(()=>{ tick++; const gr=3+2.5*Math.abs(Math.sin(tick/3)); $('#gr').style.height=gr/12*100+'%'; $('#grv').textContent='-'+gr.toFixed(1);
	$('#ml').style.height=(55+30*Math.abs(Math.sin(tick/2.3)))+'%'; $('#mr').style.height=(52+30*Math.abs(Math.sin(tick/2.1+1)))+'%';
	$$('.cs .vm b').forEach((b,j)=>{ const i=j>>1; b.style.height=(i===6?0:(15+60*Math.abs(Math.sin(tick/(2+i%5)+i+(j&1)*0.4))))+'%'; });
	$$('.st').forEach(e=>e.classList.toggle('play',+e.dataset.i===tick%16));
	$$('.k').forEach(k=>k.animMod&&k.animMod(tick));
	if(tick>12){ for(let i=0;i<16;i++) LED[i]*=0.8; const st=tick%16; if([0,4,8,12].includes(st)) LED[0]=1; if([4,12].includes(st)) LED[1]=1; if(st%2===0) LED[8]=0.9; }
	drawLeds(); const sc=$('#g-mdisp .scope'); if(sc) sc.setAttribute('stroke-opacity',(0.3+0.7*LED[state.track]).toFixed(2));
	$('#pos').textContent=`${1+Math.floor(tick/16)%4}.${1+Math.floor(tick/4)%4}.${1+tick%4}`; },120);
document.documentElement.dataset.acc='vel';
// CPU: the only value with a % sign; the bar spans a whole core, amber over the 15 % budget, red over 25 %
function setCpu(v){ $('#cpuv').textContent=v.toFixed(1)+'%'; $('#cpub').style.width=Math.min(100,v)+'%'; $('#cpu').dataset.lvl=v>25?'over':(v>15?'warn':'ok'); }
setCpu(6.8);
