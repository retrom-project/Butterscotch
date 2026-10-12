// Render the repository's non-commercial Loritta fixture through the shipped Web core.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const assets = resolve(process.argv[2]);
const require = createRequire(resolve(process.env.RETROM_PLAYWRIGHT_ROOT ?? root, 'package.json'));
const {chromium} = require('playwright');
const workerSource = `
import factory from '/butterscotch.mjs';
let runtime, canvas;
onmessage = async ({data}) => {
  try {
    if (data.canvas) {
      canvas = data.canvas;
      canvas.id = 'canvas';
      runtime = await factory({canvas, locateFile: path => '/' + path});
      const pointer = runtime._malloc(12);
      runtime.HEAP32[pointer >> 2] = canvas.width;
      runtime.HEAP32[(pointer + 4) >> 2] = canvas.height;
      runtime.HEAPU32[(pointer + 8) >> 2] = 0;
      runtime.GL.offscreenCanvases.canvas = {canvasSharedPtr: pointer, id: 'canvas', offscreenCanvas: canvas};
      postMessage({buffer: runtime.HEAPU8.buffer, offset: runtime._getContentReadSlot()});
      if (runtime.ccall('registerContentFile', 'number', ['string', 'number', 'number'], ['/content/data.win', 0, data.size]) !== 0) throw Error('register');
      runtime.ccall('startRunner', null, ['string', 'string'], ['/content/data.win', '/saves']);
    } else if (data.capture) {
      postMessage({image: await canvas.convertToBlob({type: 'image/png'})});
    }
  } catch (error) { postMessage({error: String(error)}); }
};`;
const html = `<!doctype html><canvas id="canvas" width="640" height="480"></canvas><script>
window.result = {images: [], error: null};
(async () => {
 const bytes = new Uint8Array(await (await fetch('/fixture.win')).arrayBuffer());
 const worker = new Worker('/worker.mjs', {type: 'module'});
 worker.onmessage = async ({data}) => {
  if (data.error) result.error = data.error;
  if (data.buffer) {
   const control = new Int32Array(data.buffer, data.offset, 16);
   const payload = new Uint8Array(data.buffer, data.offset + 64, 256 * 1024);
   if (Atomics.load(control, 1) !== 1) throw Error('content ABI');
   setInterval(() => {
    if (Atomics.load(control, 0) !== 1) return;
    const offset = (Atomics.load(control, 10) >>> 0) + (Atomics.load(control, 11) >>> 0) * 4294967296;
    const length = Atomics.load(control, 7);
    if (Atomics.load(control, 9) !== 0 || offset + length > bytes.length || length > payload.length) { result.error = 'content bounds'; return; }
    payload.set(bytes.subarray(offset, offset + length));
    Atomics.store(control, 4, length); Atomics.store(control, 0, 2); Atomics.notify(control, 0);
   }, 1);
  }
  if (data.image) {
   const image = await createImageBitmap(data.image);
   const sample = new OffscreenCanvas(image.width, image.height), ctx = sample.getContext('2d');
   ctx.drawImage(image, 0, 0);
   const rgba = ctx.getImageData(0, 0, image.width, image.height).data;
   let nonBlack = 0; const colors = new Set();
   for (let i = 0; i < rgba.length; i += 4) {
    if (rgba[i] + rgba[i + 1] + rgba[i + 2] > 30) nonBlack++;
    colors.add(rgba[i] * 65536 + rgba[i + 1] * 256 + rgba[i + 2]);
   }
   result.images.push({nonBlack, colors: colors.size}); image.close();
  }
 };
 const canvas = document.querySelector('canvas').transferControlToOffscreen();
 worker.postMessage({canvas, size: bytes.length}, [canvas]);
 window.capture = () => worker.postMessage({capture: true});
})().catch(error => result.error = String(error));
</script>`;
const server = createServer(async (request, response) => {
  response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  const path = new URL(request.url, 'http://localhost').pathname;
  try {
    if (path === '/') {response.setHeader('Content-Type', 'text/html'); response.end(html);}
    else if (path === '/worker.mjs') {response.setHeader('Content-Type', 'text/javascript'); response.end(workerSource);}
    else if (path === '/fixture.win') {response.end(await readFile(resolve(root, 'tests/loritta-and-the-stars/data.win')));}
    else if (['/butterscotch.mjs', '/butterscotch.wasm'].includes(path)) {
      response.setHeader('Content-Type', path.endsWith('.wasm') ? 'application/wasm' : 'text/javascript');
      response.end(await readFile(resolve(assets, path.slice(1))));
    } else {response.writeHead(404).end();}
  } catch (error) {response.writeHead(500).end(String(error));}
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({headless: true, ...(process.env.RETROM_CHROME_EXECUTABLE ? {executablePath: process.env.RETROM_CHROME_EXECUTABLE} : {})});
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('console', message => {if (message.type() === 'error') errors.push(message.text());});
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.waitForFunction(() => typeof window.capture === 'function');
  await page.waitForTimeout(5000);
  for (let i = 0; i < 2; i++) {
    await page.evaluate(() => window.capture());
    await page.waitForFunction(count => window.result.images.length > count || window.result.error, i);
    await page.waitForTimeout(500);
  }
  const result = await page.evaluate(() => window.result);
  assert.equal(result.error, null);
  assert.deepEqual(errors.filter(error => !error.includes('404')), []);
  assert.equal(result.images.length, 2);
  for (const sample of result.images) {
    assert.ok(sample.nonBlack > 1000 && sample.colors > 4, `Expected rendered game textures, got ${JSON.stringify(sample)}`);
  }
  console.log(JSON.stringify({test: 'Web core renders Loritta textures and application surface', ...result}));
} finally {
  await browser?.close(); await new Promise(resolve => server.close(resolve));
}
