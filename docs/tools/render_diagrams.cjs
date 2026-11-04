// Render the four Mermaid blocks in the overview without changing their source.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

async function main() {
  const [mermaidPath, browserPath] = process.argv.slice(2);
  if (!mermaidPath || !browserPath) {
    throw new Error('Usage: node render_diagrams.cjs <mermaid.min.js> <browser executable>');
  }
  const docs = path.resolve(__dirname, '..');
  const source = fs.readFileSync(path.join(docs, 'device-overview-ru.md'), 'utf8');
  const blocks = [...source.matchAll(/```mermaid\r?\n([\s\S]*?)```/g)].map(match => match[1]);
  const names = ['hardware-architecture', 'software-architecture', 'light-control', 'diagnostics-flow'];
  if (blocks.length !== names.length) throw new Error('Expected exactly four Mermaid diagrams');
  fs.mkdirSync(path.join(docs, 'assets'), { recursive: true });
  const browser = await chromium.launch({ executablePath: browserPath, headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1600, height: 1200 }, deviceScaleFactor: 2 });
    await page.setContent('<!doctype html><html lang="ru"><meta charset="utf-8"><style>body{margin:0;background:white}#figure{display:inline-block;padding:20px}svg{max-width:none!important}</style><div id="figure"></div></html>');
    await page.addScriptTag({ path: path.resolve(mermaidPath) });
    await page.evaluate(() => mermaid.initialize({
      startOnLoad: false, securityLevel: 'strict', theme: 'base',
      deterministicIds: true, deterministicIDSeed: 'grow-system-v02',
      fontFamily: 'Arial', themeVariables: { fontSize: '17px', primaryColor: '#edf4fa',
        primaryTextColor: '#172b42', primaryBorderColor: '#49759b', lineColor: '#49759b',
        secondaryColor: '#eef7f1', tertiaryColor: '#fff7e7' },
      flowchart: { htmlLabels: false, curve: 'linear', nodeSpacing: 30, rankSpacing: 45 }
    }));
    for (let i = 0; i < blocks.length; i++) {
      const svg = await page.evaluate(async ({ source, id }) => {
        await mermaid.parse(source);
        return (await mermaid.render(id, source)).svg;
      }, { source: blocks[i], id: names[i] });
      fs.writeFileSync(path.join(docs, 'assets', names[i] + '.svg'), svg + '\n');
      await page.locator('#figure').evaluate((element, svg) => {
        element.innerHTML = svg;
        const drawing = element.querySelector('svg');
        const box = drawing.viewBox.baseVal;
        drawing.setAttribute('width', box.width);
        drawing.setAttribute('height', box.height);
      }, svg);
      await page.locator('#figure').screenshot({ path: path.join(docs, 'assets', names[i] + '.png') });
      console.log('Validated and rendered: ' + names[i]);
    }
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
