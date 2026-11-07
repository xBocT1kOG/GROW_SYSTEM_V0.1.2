// Rasterize translated SVGs without recalculating any data. Fonts stay local.
// Usage: node render_localized_figures.cjs <node_modules> <browser.exe> <DejaVu-font-dir>
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(path.join(process.argv[2], 'playwright'));
const fontDir = process.argv[4];
const figures = {'health-summary':405, 'error-counters':972, heap:738,
                 'stack-margins':684, 'lighting-activity':810};
(async () => {
  const browser = await chromium.launch({executablePath:process.argv[3], headless:true});
  try {
    const page = await browser.newPage({deviceScaleFactor:1});
    const faces = [['DejaVuSans.ttf',400],['DejaVuSans-Bold.ttf',700]].map(([file,weight]) =>
      `@font-face {font-family:'DejaVu Sans';font-weight:${weight};src:url(data:font/ttf;base64,${fs.readFileSync(path.join(fontDir,file)).toString('base64')})}`).join('\n');
    for (const lang of ['uk','en']) for (const [stem,height] of Object.entries(figures)) {
      const base = path.join(__dirname,'assets',`${stem}-${lang}`);
      const svg = fs.readFileSync(base+'.svg','utf8').replace(/^[\s\S]*?(<svg\b)/,'$1');
      await page.setViewportSize({width:1620,height});
      await page.setContent(`<style>${faces}html,body{margin:0}svg{display:block;width:1620px;height:${height}px}</style>${svg}`);
      await page.evaluate(async () => {
        await document.fonts.load('10px "DejaVu Sans"');
        await document.fonts.load('700 10px "DejaVu Sans"');
        await document.fonts.ready;
      });
      const overflow = await page.locator('svg text').evaluateAll(nodes => nodes.filter(n => {
        const r=n.getBoundingClientRect(), s=n.ownerSVGElement.getBoundingClientRect();
        return r.left<s.left-1 || r.right>s.right+1 || r.top<s.top-1 || r.bottom>s.bottom+1;
      }).map(n=>n.textContent));
      if(overflow.length) throw new Error(`${stem}-${lang}: text outside chart: ${overflow}`);
      await page.screenshot({path:base+'.png'});
      console.log(`${stem}-${lang}.png`);
    }
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
