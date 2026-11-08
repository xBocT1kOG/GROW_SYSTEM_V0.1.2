import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { languages, slides, sources } from './content.mjs';
import { notes, timingSeconds } from './notes.mjs';

// Explicit runtime arguments keep machine-specific paths out of the source.
const [modules, skillDir, pythonExecutable, outputArgument] = process.argv.slice(2);
if (!modules || !skillDir || !pythonExecutable) throw new Error('Usage: node build.mjs NODE_MODULES SKILL_DIR PYTHON [OUTPUT_DIRECTORY]');
process.env.RUNTIME_NODE_MODULES=modules;
const { Presentation, PresentationFile } = await import(pathToFileURL(path.join(modules, '@oai/artifact-tool/dist/artifact_tool.mjs')));
const { finalizePresentation } = await import(pathToFileURL(path.join(skillDir, 'container_tools/artifact_tool_utils.mjs')));
const here = path.dirname(fileURLToPath(import.meta.url));
const workspaceDir = path.resolve(here, '../..');
const outputDir = path.resolve(outputArgument || here);
const buildDir = path.join(workspaceDir, '.pio/presentation-build');
await fs.mkdir(outputDir, { recursive: true });
await fs.mkdir(buildDir, { recursive: true });
const image = await fs.readFile(path.join(here, '../assets/blynk-dashboard.png'));
const screenshot = image.buffer.slice(image.byteOffset, image.byteOffset + image.byteLength);
const C = { bg:'#101820', panel:'#1B2833', ink:'#F4F7FA', muted:'#B7C6CF', accent:'#54D6B7', edge:'#3A5362' };
const geometry = [];
const revision = Date.now().toString();
let activeLanguage, activeSlide;

function box(s, x, y, w, h, fill=C.panel, stroke=C.edge) {
  return s.shapes.add({ geometry:'rect', position:{left:x,top:y,width:w,height:h}, fill,
    line:{fill:stroke,width:stroke==='none'?0:1,style:'solid'} });
}
function text(s, value, x, y, w, h, size=26, color=C.ink, bold=false, align='left') {
  const sh=box(s,x,y,w,h,'none','none');
  sh.text=value;
  sh.text.style={ typeface:'Arial', fontSize:size, color, bold:!!bold, alignment:align, insets:0, verticalAlignment:'top', autoFit:'none' };
  geometry.push({lang:activeLanguage,slide:activeSlide,text:value,x,y,w,h,size});
  return sh;
}
function card(s, value, x,y,w,h, size=25, accent=false) {
  const sh=box(s,x,y,w,h,accent?'#183B38':C.panel,accent?C.accent:C.edge);
  text(s,value,x+16,y+15,w-32,h-22,size,accent?C.accent:C.ink,true);
  return sh;
}
function link(s,a,b,from='right',to='left',dashed=false) {
  return s.shapes.connect(a,b,{kind:'straight',fromSide:from,toSide:to,
    line:{fill:C.accent,width:2,style:dashed?'dashed':'solid'},tail:{type:'arrow',width:'med',length:'med'}});
}
function footer(s, n, lang) {
  box(s,56,672,1168,1,C.edge,'none');
  text(s,'GROW SYSTEM V0.2  /  ESP32-S3 · FreeRTOS',56,683,900,24,16,C.muted);
  text(s,`${lang}  /  ${String(n).padStart(2,'0')}`,1110,683,114,24,16,C.muted,false,'right');
}
function header(s,d,L,n) {
  text(s,d.title[L],56,43,1168,65,44,C.ink,true);
  if(d.subtitle)text(s,d.subtitle[L],56,114,1168,65,25,C.muted);
  footer(s,n,languages[L]);
}

for (const [L,lang] of languages.entries()) {
  activeLanguage=lang;
  const p=Presentation.create({slideSize:{width:1280,height:720}});
  p.theme.defaultFont='Arial';
  const dir=path.join(buildDir,lang); await fs.mkdir(dir,{recursive:true});
  for (const [i,d] of slides.entries()) {
    activeSlide=i+1;
    const s=p.slides.add(); s.background.fill=C.bg;
    const t=v=>v[L];
    if(i===0) {
      text(s,'ESP32-S3 / FreeRTOS',56,72,1100,45,26,C.accent,true);
      text(s,t(d.title),56,176,1168,100,72,C.ink,true);
      text(s,t(d.subtitle),56,290,1080,112,40,C.ink);
      text(s,t(d.strap),56,443,1168,48,28,C.muted);
      d.labels.forEach((v,j)=>{ text(s,t(v),56+j*400,560,370,50,29,C.accent,true); });
      footer(s,1,lang);
    } else {
      header(s,d,L,i+1);
      if(i===1) {
        d.labels.forEach((v,j)=>{text(s,`0${j+1}`,56,213+j*87,55,44,28,C.accent,true);text(s,t(v),125,213+j*87,465,66,28);});
        box(s,655,190,569,356);
        text(s,t(d.europe),680,207,510,40,27,C.accent,true);
        text(s,t(d.ex1),680,258,510,38,25,C.ink,true);
        text(s,t(d.ex1detail),680,301,510,80,26);
        text(s,'WATERBEE',680,402,510,35,25,C.ink,true);
        text(s,t(d.ex2detail),680,445,510,80,26);
        text(s,t(d.caveat),655,558,569,75,21,C.muted);
        const refs=text(s,'EU CAP Network · IRRISMA    |    European Commission CORDIS · 283638',56,636,1168,27,17,C.muted);
        refs.text.get('EU CAP Network · IRRISMA').link={uri:sources.irrisma,isExternal:true};
        refs.text.get('European Commission CORDIS · 283638').link={uri:sources.waterbee,isExternal:true};
      } else if(i===2) {
        box(s,56,200,1168,150);
        text(s,'V0.1',80,219,125,50,36,C.muted,true);
        text(s,t(d.v1),240,216,950,43,30,C.ink,true);
        text(s,t(d.v1flow),240,279,950,49,26,C.muted);
        box(s,56,379,1168,199,'#183B38',C.accent);
        text(s,'V0.2',80,400,125,50,36,C.accent,true);
        text(s,t(d.v2),240,400,950,45,30,C.ink,true);
        d.labels.forEach((v,j)=>text(s,t(v),240+j*320,471,300,80,25,C.accent));
        text(s,t(d.takeaway),56,607,1168,50,26,C.muted);
      } else if(i===3) {
        const sensor=box(s,56,200,370,250);text(s,t(d.sensors),76,219,330,33,23,C.accent,true);text(s,t(d.sensorLines),76,270,330,165,24);
        const cpu=card(s,'ESP32-S3',488,279,285,90,33,true);
        const act=box(s,845,200,379,250);text(s,t(d.actuators),865,219,339,65,23,C.accent,true);text(s,t(d.actuatorLines),865,305,339,120,26);
        link(s,sensor,cpu);link(s,cpu,act);
        const local=box(s,488,434,285,142);text(s,t(d.local),504,447,253,54,20,C.accent,true);text(s,t(d.localLines),504,502,253,66,24);link(s,local,cpu,'top','bottom');
        card(s,t(d.logic),56,499,370,77,26);card(s,t(d.power),845,499,379,77,26);
        text(s,t(d.caveat),56,617,1168,40,21,C.muted);
      } else if(i===4) {
        const tasks=['sensor_task','input_task','time_task','ramp_task','network_task'].map((v,j)=>card(s,v,56,195+j*66,218,52,25));
        const q=card(s,t(d.queue),330,299,165,100,26);
        const control=card(s,'control_task',545,299,224,100,28,true);
        const state=card(s,t(d.state),810,299,190,114,22);
        const drivers=card(s,t(d.drivers),1047,279,177,63,23);
        const hardware=card(s,t(d.hardware),1047,406,177,75,20);
        tasks.forEach(a=>link(s,a,q));link(s,q,control);link(s,control,state);link(s,state,drivers);link(s,drivers,hardware,'bottom','top');
        const snap=card(s,t(d.snapshots),330,451,699,55,24);link(s,control,snap,'bottom','top',true);
        const readers=card(s,t(d.readers),330,539,894,57,22);link(s,snap,readers,'bottom','top',true);
        text(s,t(d.diagnostics),330,618,894,42,22,C.muted);
      } else if(i===5) {
        const a=card(s,`DAWN\n${t(d.dawn)}`,56,220,350,124,29,true);
        const b=card(s,`IDLE\n${t(d.idle)}`,464,220,350,124,29);
        const c=card(s,`DUSK\n${t(d.dusk)}`,872,220,352,124,29,true);
        link(s,a,b);link(s,b,c);
        text(s,t(d.step),56,371,1168,52,32,C.accent,true,'center');
        d.fields.forEach((v,j)=>text(s,t(v),56+(j%2)*605,454+Math.floor(j/2)*67,563,53,26));
        text(s,t(d.takeaway),56,605,1168,52,24,C.muted);
      } else if(i===6) {
        s.images.add({blob:screenshot,contentType:'image/png',alt:'Actual GROW SYSTEM Blynk desktop dashboard',fit:'contain',position:{left:225,top:181,width:830,height:460}});
        text(s,t(d.takeaway),56,641,1168,27,22,C.accent,true,'center');
      } else if(i===7) {
        d.groups.forEach((v,j)=>{box(s,56+j*397,195,374,152);text(s,t(v),73+j*397,211,340,42,26,C.accent,true);text(s,t(d.details[j]),73+j*397,262,340,75,25);});
        const esp=card(s,'ESP32',56,395,150,84,27,true);
        const api=card(s,'/api/state\n/api/diagnostics',253,380,282,114,24);
        const pc=card(s,'PC_collector',582,395,231,84,27);
        const csv=card(s,'CSV',860,395,130,84,27);
        const analysis=card(s,t(d.analysis),1037,395,187,84,27);
        link(s,esp,api);link(s,api,pc);link(s,pc,csv);link(s,csv,analysis);
        text(s,t(d.ready),56,531,1168,49,31,C.accent,true);
        text(s,t(d.caveat),56,591,1168,71,23,C.muted);
      } else if(i===8) {
        const hub=card(s,'GROW SYSTEM V0.2',385,215,510,95,37,true);
        d.labels.forEach((v,j)=>{const a=box(s,56+j*237,382,220,168);text(s,t(v),72+j*237,400,188,72,23,C.accent,true);text(s,t(d.details[j]),72+j*237,484,188,54,23);link(s,hub,a,'bottom','top');});
        text(s,t(d.takeaway),56,583,1168,43,30,C.ink,true);
        text(s,t(d.caveat),56,634,1168,28,22,C.muted);
      } else if(i===9) {
        const root=card(s,'GROW SYSTEM',440,132,400,66,31,true);
        const pro=box(s,56,235,572,300);const home=box(s,652,235,572,300);link(s,root,pro,'bottom','top');link(s,root,home,'bottom','top');
        text(s,t(d.pro),76,252,532,47,25,C.accent,true);text(s,t(d.home),672,252,532,47,25,C.accent,true);
        text(s,t(d.proLines),76,314,532,132,24);text(s,t(d.homeLines),672,314,532,171,24);
        text(s,t(d.llm),76,452,532,73,23,C.accent);
        text(s,t(d.future),56,549,1168,32,21,C.muted,'','center');
        text(s,t(d.takeaway),56,593,1168,73,30,C.ink,true,'center');
      }
    }
    let reference=`Sources: docs/device-overview-${lang.toLowerCase()}.md; corresponding GROW_SYSTEM_V0.2_Technical_Overview_${lang}.pdf.`;
    if(i===1)reference=`Sources: EU CAP Network — IRRISMA: ${sources.irrisma}\nEuropean Commission CORDIS — WATERBEE project 283638: ${sources.waterbee}\nAccessed 2026-11-04. External context only; not GROW SYSTEM performance.`;
    if(i===2 || i===9)reference+=' Project history / future directions: user-approved presentation brief.';
    if(i===6)reference+=' Actual project screenshot: docs/assets/blynk-dashboard.png; displayed values are illustrative, not validation evidence.';
    s.speakerNotes.text=`${notes[lang][i]}\n\n[${timingSeconds[i]} s; total planned delivery ${timingSeconds.reduce((a,b)=>a+b,0)} s]\n${reference}`;
    const png=await p.export({slide:s,format:'png',scale:1});
    await fs.writeFile(path.join(dir,`${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
    const layout=await s.export({format:'layout'});await fs.writeFile(path.join(dir,`${i+1}.layout.json`),await layout.text());
    console.log(`${lang} slide ${i+1}: rendered`);
  }
  const candidatePath=path.join(dir,`candidate-${revision}.pptx`);
  await (await PresentationFile.exportPptx(p)).save(candidatePath);
  const finalPath=path.join(outputDir,`GROW_SYSTEM_V0.2_Presentation_${lang}.pptx`);
  const result=await finalizePresentation({ workspaceDir,candidatePath,finalPath,pythonExecutable,
    explicitTotalSlideCount:10,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],
    integrityValidatorPath:path.join(skillDir,'container_tools/inspect_presentation_package_integrity.py'),
    layoutValidatorPath:path.join(skillDir,'container_tools/inspect_presentation_layout_geometry.py'),
    layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-bullet-geometry','--validate-heading-fit'],
    fontPolicy:{basis:'design',families:['Arial']},verifyArtifactToolImport:true,
    receiptPath:path.join(dir,`validation-${revision}.json`) });
  console.log(`${lang}: finalized ${finalPath}`);
}
await fs.writeFile(path.join(buildDir,'authored-geometry.json'),JSON.stringify(geometry,null,2));
