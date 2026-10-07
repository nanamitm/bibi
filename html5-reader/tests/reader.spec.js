import { test, expect } from '@playwright/test';
import JSZip from 'jszip';
async function reviewEpub({ missingNavigation = false, body = '<p>First chapter</p>', head = '', title = 'First' } = {}) {
  const zip = new JSZip();
  zip.file('mimetype', 'application/epub+zip');
  zip.file('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf"/></rootfiles></container>');
  zip.file('OEBPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">review</dc:identifier><dc:title>Review</dc:title><dc:language>ja</dc:language></metadata><manifest><item id="one" href="Text/one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="Text/two.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>');
  if (!missingNavigation) zip.file('OEBPS/nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol><li><a href="Text/one.xhtml">One</a></li><li><a href="Text/two.xhtml">Two</a></li></ol></nav></body></html>');
  zip.file('OEBPS/Text/one.xhtml', `<html xmlns="http://www.w3.org/1999/xhtml"><head><title>${title}</title>${head}</head><body>${body}</body></html>`);
  zip.file('OEBPS/Text/two.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Second</title></head><body><h1 id="destination">Second chapter</h1></body></html>');
  return {name:'review.epub', mimeType:'application/epub+zip', buffer:await zip.generateAsync({type:'nodebuffer'})};
}

test('missing navigation opens with a spine TOC and can open another book', async ({page}) => {
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await page.locator('#file').setInputFiles(await reviewEpub({missingNavigation:true}));
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  await expect(page.locator('#toc')).toContainText('章 1');
  await expect(page.locator('#toc')).toContainText('章 2');
  await page.locator('#toc').getByText('章 2').click();
  await expect(page.locator('#status')).toContainText('章 2 / 2');
  await expect(page.locator('#open')).toBeEnabled();
  await page.locator('#file').setInputFiles(await reviewEpub());
  await expect(page.locator('#toc')).toContainText('One');
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  expect(errors).toEqual([]);
});

test('relative chapter and fragment links resolve inside the EPUB', async ({page}) => {
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await page.locator('#file').setInputFiles(await reviewEpub({
    head: '<base href="https://example.org/" target="_top"/>',
    body: '<h1 id="first">First chapter</h1><a href="#first">Same chapter</a><a href="two.xhtml#destination">Next chapter</a>',
  }));
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  await page.frameLocator('iframe').getByRole('link', {name:'Same chapter'}).click();
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  await page.frameLocator('iframe').getByRole('link', {name:'Next chapter'}).click();
  await expect(page.locator('#status')).toContainText('章 2 / 2');
  await expect(page.frameLocator('iframe').locator('#destination')).toBeVisible();
  expect(errors).toEqual([]);
});

test('search excludes title, CSS, ruby readings and hidden content', async ({page}) => {
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await page.locator('#file').setInputFiles(await reviewEpub({
    title:'Needle title', head:'<style>.Needle{color:red}</style>',
    body:'<p>Visible Needle C++</p><p hidden="hidden">Hidden Needle</p><p style="display:none">Styled Needle</p><div style="visibility:hidden"><p>Invisible Needle</p><p style="visibility:visible">Revealed Needle</p></div><p aria-hidden="true">Decorative Needle</p><svg xmlns="http://www.w3.org/2000/svg"><text>Graphic Needle</text></svg><ruby>Base<rt>Needle</rt></ruby>',
  }));
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  await page.locator('[data-tab=search]').click();
  await page.locator('#query').fill('needle');
  await page.locator('#search-button').click();
  await expect(page.locator('#search-status')).toHaveText('2件');
  await expect(page.locator('#results')).toContainText('Visible Needle');
  await expect(page.locator('#results')).toContainText('Revealed Needle');
  await expect(page.locator('#results')).not.toContainText(/Invisible|Decorative|Graphic/);
  await page.locator('#results button').first().click();
  await expect(page.frameLocator('iframe').locator('body')).toContainText('Visible Needle');
  await page.locator('#query').fill('C++');
  await page.locator('#search-button').click();
  await expect(page.locator('#search-status')).toHaveText('1件');
  expect(errors).toEqual([]);
});

test('sample: vertical reading, TOC, search, bookmarks, persisted position and settings', async ({page}) => {
  const errors=[]; page.on('pageerror', error=>{errors.push(error.message); console.log(error.message);});
  await page.goto('./');
  await page.getByRole('button',{name:'サンプルを読む'}).click();
  await expect(page.locator('#title')).toContainText('読書の時間');
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  await expect(page.locator('iframe')).toHaveCount(1);
  await expect(page.frameLocator('iframe').locator('body')).toContainText('朝の光');
  expect(await page.frameLocator('iframe').locator('html').evaluate(el=>getComputedStyle(el).writingMode)).toBe('vertical-rl');
  await page.screenshot({path:test.info().outputPath('sample.png')});
  await page.getByRole('button',{name:'第二章 夜の読書',exact:true}).click();
  await expect(page.locator('#status')).toContainText('章 2 / 2');
  await page.locator('[data-tab=bookmarks]').click();
  await page.locator('#bookmark').click();
  await expect(page.locator('#bookmark-list li')).toHaveCount(1);
  await page.locator('#size').selectOption('120');
  await page.locator('#theme').selectOption('sepia');
  await page.reload();
  await expect(page.locator('#size')).toHaveValue('120');
  await page.locator('#sample').click();
  await expect(page.locator('#status')).toContainText('章 2 / 2');
  await page.locator('[data-tab=bookmarks]').click();
  await expect(page.locator('#bookmark-list li')).toHaveCount(1);
  await page.locator('[data-tab=search]').click();
  await page.locator('#query').fill('朝の光');
  await page.locator('#search-button').click();
  await expect(page.locator('#search-status')).toHaveText('12件');
  await page.locator('#results button').first().click();
  await expect(page.locator('#status')).toContainText('章 1 / 2');
  expect(errors).toEqual([]);
});
test('invalid EPUB recovers and allows another book', async ({page})=>{
  await page.goto('./');
  await page.locator('#file').setInputFiles({name:'broken.epub',mimeType:'application/epub+zip',buffer:Buffer.from('broken')});
  await expect(page.locator('#status')).toContainText('読み込みに失敗');
  await expect(page.locator('#open')).toBeEnabled();
  await page.locator('#sample').click();
  await expect(page.locator('#status')).toContainText('章 1 / 2');
});
test('EPUB2 NCX, embedded assets and hostile content', async ({page})=>{
  const zip=new JSZip();
  zip.file('mimetype','application/epub+zip');
  zip.file('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf"/></rootfiles></container>');
  zip.file('OEBPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">test</dc:identifier><dc:title>EPUB2 test</dc:title><dc:language>en</dc:language></metadata><manifest><item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/><item id="img" href="image.svg" media-type="image/svg+xml"/></manifest><spine toc="ncx"><itemref idref="c"/></spine></package>');
  zip.file('OEBPS/toc.ncx','<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><navMap><navPoint id="n"><navLabel><text>Test chapter</text></navLabel><content src="chapter.xhtml"/></navPoint></navMap></ncx>');
  zip.file('OEBPS/image.svg','<svg xmlns="http://www.w3.org/2000/svg" width="50" height="50"><rect width="50" height="50" fill="green"/></svg>');
  zip.file('OEBPS/chapter.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Test</title><script>parent.hacked=true</script></head><body onload="parent.hacked=true"><h1>Hello reader</h1><img src="image.svg"/><img src="https://example.org/tracker.png"/><script>parent.hacked=true</script></body></html>');
  const requests=[]; page.on('request',request=>{if(request.url().startsWith('https://example.org')) requests.push(request.url());});
  await page.goto('./');
  await page.locator('#file').setInputFiles({name:'test.epub',mimeType:'application/epub+zip',buffer:await zip.generateAsync({type:'nodebuffer'})});
  await expect(page.locator('#status')).toContainText('章 1 / 1');
  await expect(page.locator('#toc')).toContainText('Test chapter');
  await expect(page.frameLocator('iframe').locator('body')).toContainText('Hello reader');
  await expect.poll(()=>page.frameLocator('iframe').locator('img').first().evaluate(img=>img.naturalWidth)).toBe(50);
  expect(await page.evaluate(()=>window.hacked)).toBeUndefined();
  expect(requests).toEqual([]);
});
test('mobile can open sample and toggle navigation',async({page})=>{
  await page.setViewportSize({width:390,height:844});await page.goto('./');await page.locator('#sample').click();
  await expect(page.locator('#status')).toContainText('章 1 / 2');await expect(page.locator('#sidebar')).toBeHidden();
  await page.locator('#sidebar-toggle').click();await expect(page.locator('#sidebar')).toBeVisible();
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true);
});

