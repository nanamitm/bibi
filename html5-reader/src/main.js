import ePub from 'epubjs';
import JSZip from 'jszip';
import { findBodyMatches } from './body-search.js';
import './style.css';

const $ = (id) => document.getElementById(id);
let book, rendition, bookKey, current, generation = 0, searchGeneration = 0;
let opening = false;
const palettes = { light: ['#ffffff', '#202b24'], sepia: ['#f5ecd9', '#392f23'], dark: ['#202421', '#e1e7df'] };
function read(key, fallback) { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } }
function save(key, value) { try { localStorage.setItem(key, JSON.stringify(value)); } catch { status('ブラウザーの保存領域が利用できません。読書は続けられます。'); } }
function status(message, error = false) { $('status').textContent = message; $('status').classList.toggle('error', error); }
function report(error) { console.error(error); status(`読み込みに失敗しました: ${error.message || error}`, true); }
function navigate(target) { return rendition?.display(target).catch(report); }
function applySettings() {
  document.body.dataset.theme = $('theme').value;
  if (rendition) {
    const [background, color] = palettes[$('theme').value];
    rendition.themes.default({ body: { 'background-color': `${background} !important`, color: `${color} !important` }, a: { color: $('theme').value === 'dark' ? '#a6cdb4' : '#315540' } });
    rendition.themes.fontSize(`${$('size').value}%`);
  }
  save('bibi-html5-settings', { size: $('size').value, theme: $('theme').value });
}
const settings = read('bibi-html5-settings', {});
if (['80','100','120','150','200'].includes(settings.size)) $('size').value = settings.size;
if (settings.theme in palettes) $('theme').value = settings.theme;
applySettings();
$('size').onchange = $('theme').onchange = applySettings;
function hideMobilePanel() { if (innerWidth <= 700) { $('sidebar').hidden = true; $('sidebar-toggle').setAttribute('aria-expanded', 'false'); } }
hideMobilePanel();
$('sidebar-toggle').onclick = () => { $('sidebar').hidden = !$('sidebar').hidden; $('sidebar-toggle').setAttribute('aria-expanded', String(!$('sidebar').hidden)); };
for (const button of document.querySelectorAll('[data-tab]')) button.onclick = () => {
  for (const tab of document.querySelectorAll('[data-tab]')) { const active = tab === button; tab.setAttribute('aria-pressed', String(active)); $(tab.dataset.tab).hidden = !active; }
};
function tocList(items) {
  const list = document.createElement('ul');
  for (const item of items) {
    const li = document.createElement('li'), button = document.createElement('button');
    button.textContent = item.label.trim() || '無題';
    button.onclick = () => { navigate(item.href); hideMobilePanel(); };
    li.append(button);
    if (item.subitems?.length) li.append(tocList(item.subitems));
    list.append(li);
  }
  return list;
}
function renderBookmarks() {
  $('bookmark-list').replaceChildren();
  const marks = bookKey ? read(`${bookKey}-marks`, []) : [];
  for (const [index, mark] of marks.entries()) {
    const li = document.createElement('li'), jump = document.createElement('button'), remove = document.createElement('button');
    jump.textContent = mark.label; jump.onclick = () => { navigate(mark.cfi); hideMobilePanel(); };
    remove.textContent = '削除'; remove.setAttribute('aria-label', `${mark.label}を削除`);
    remove.onclick = () => { marks.splice(index, 1); save(`${bookKey}-marks`, marks); renderBookmarks(); };
    li.append(jump, remove); $('bookmark-list').append(li);
  }
  if (!marks.length) $('bookmark-list').textContent = 'しおりはありません。';
}
$('bookmark').onclick = () => {
  if (!current?.start?.cfi) return;
  const marks = read(`${bookKey}-marks`, []);
  if (!marks.some((mark) => mark.cfi === current.start.cfi)) {
    marks.push({ cfi: current.start.cfi, label: `章 ${current.start.index + 1} · ${new Date().toLocaleString('ja-JP')}` });
    save(`${bookKey}-marks`, marks); renderBookmarks(); status('しおりを保存しました');
  }
};
async function openFile(file) {
  if (!file || opening) return;
  if (!/\.epub$/i.test(file.name)) { status('EPUBファイルを選択してください', true); return; }
  opening = true; $('open').disabled = $('sample').disabled = true;
  const token = ++generation; ++searchGeneration;
  $('search-button').disabled = true;
  $('results').replaceChildren(); $('search-status').textContent = '';
  $('previous').disabled = $('next').disabled = $('bookmark').disabled = true;
  current = null; bookKey = null; renderBookmarks(); $('toc').replaceChildren();
  rendition?.destroy(); book?.destroy(); rendition = null; book = null;
  $('viewer').hidden = true; $('welcome').hidden = false;
  status('EPUBを読み込んでいます…');
  try {
    const bytes = await file.arrayBuffer();
    const hash = await crypto.subtle.digest('SHA-256', bytes);
    bookKey = `bibi-html5-${Array.from(new Uint8Array(hash), (b) => b.toString(16).padStart(2, '0')).join('')}`;
    const nextBook = ePub(); book = nextBook;
    // epub.js 0.3.93 resolves loaded.navigation only on success in unpack().
    // Forward failures to that deferred promise so ready rejects instead of
    // hanging, and consume the early rejection before open() has completed.
    const loadNavigation = nextBook.loadNavigation.bind(nextBook);
    nextBook.loadNavigation = async (packaging) => {
      try { return await loadNavigation(packaging); }
      catch (error) { nextBook.loading.navigation.reject(error); }
    };
    nextBook.ready.catch(() => {});
    // Register before opening so EPUB scripts cannot run during rendering or search.
    nextBook.spine.hooks.content.register((doc, section) => {
      doc.querySelectorAll('script,iframe,object,embed,form,base').forEach((node) => node.remove());
      // Ignore any base supplied by the book, but keep epub.js link resolution
      // anchored to this chapter instead of the reader's /bibi/ URL.
      const base = doc.createElement('base');
      base.setAttribute('href', new URL(section.url, location.origin).href);
      (doc.head || doc.documentElement).prepend(base);
      for (const node of doc.querySelectorAll('*')) for (const attr of [...node.attributes]) {
        if (/^on/i.test(attr.name) || (['href','src','action'].includes(attr.name) && /^\s*javascript:/i.test(attr.value))) node.removeAttribute(attr.name);
      }
      for (const node of doc.querySelectorAll('[src], [srcset], link[href], [poster], image')) {
        for (const attr of [...node.attributes]) {
          if (['src', 'href', 'xlink:href', 'poster', 'srcset'].includes(attr.name) && /(?:https?:)?\/\//i.test(attr.value)) node.removeAttribute(attr.name);
        }
      }
      const meta = doc.createElement('meta'); meta.setAttribute('http-equiv', 'Content-Security-Policy');
      meta.setAttribute('content', "default-src 'none'; img-src blob: data:; style-src 'unsafe-inline' blob: data:; font-src blob: data:; media-src blob: data:; script-src 'none'; frame-src 'none'; connect-src 'none'; form-action 'none'");
      (doc.head || doc.documentElement).prepend(meta);
    });
    await nextBook.open(bytes, 'binary');
    await nextBook.ready;
    if (!nextBook.spine.length) throw new Error('本文が含まれていないEPUBです');
    if (token !== generation) return;
    const metadata = await nextBook.loaded.metadata;
    $('title').textContent = [metadata.title || file.name, metadata.creator].filter(Boolean).join(' — ');
    document.title = `${metadata.title || file.name} — Bibi HTML5 Reader`;
    const navigation = await nextBook.loaded.navigation;
    const items = navigation.toc.length ? navigation.toc : nextBook.spine.spineItems.map((section, index) => ({ label: `章 ${index + 1}`, href: section.href }));
    $('toc').replaceChildren(tocList(items));
    $('welcome').hidden = true; $('viewer').hidden = false;
    rendition = nextBook.renderTo('viewer', { width: '100%', height: '100%', spread: 'none', flow: 'paginated', allowScriptedContent: false, allowPopups: false });
    rendition.on('relocated', (location) => {
      current = location; save(`${bookKey}-position`, location.start.cfi);
      $('previous').disabled = location.atStart; $('next').disabled = location.atEnd;
      status(`章 ${location.start.index + 1} / ${nextBook.spine.length} · 章内 ${location.start.displayed.page} / ${location.start.displayed.total}`);
    });
    rendition.on('keydown', handleKey);
    rendition.on('displayError', report);
    applySettings();
    const position = read(`${bookKey}-position`, null);
    try { await rendition.display(position || undefined); } catch (error) { if (!position) throw error; await rendition.display(); }
    $('bookmark').disabled = false; renderBookmarks(); hideMobilePanel();
  } catch (error) {
    rendition?.destroy(); book?.destroy(); rendition = null; book = null; bookKey = null;
    $('viewer').hidden = true; $('welcome').hidden = false; $('title').textContent = 'EPUBを開けませんでした';
    report(error);
  } finally {
    opening = false; $('open').disabled = $('sample').disabled = false; $('search-button').disabled = !book;
  }
}
$('open').onclick = () => $('file').click();
$('file').onchange = () => { const file = $('file').files[0]; $('file').value = ''; void openFile(file); };
function turn(next) { if (!rendition) return; (next ? rendition.next() : rendition.prev()).catch(report); }
$('previous').onclick = () => turn(false); $('next').onclick = () => turn(true);
function handleKey(event) {
  if (event.ctrlKey || event.metaKey || event.altKey || /INPUT|TEXTAREA|SELECT/.test(event.target?.tagName) || event.target?.isContentEditable) return;
  const rtl = book?.packaging?.metadata?.direction === 'rtl';
  if (event.key === 'ArrowRight') { event.preventDefault(); turn(!rtl); }
  if (event.key === 'ArrowLeft') { event.preventDefault(); turn(rtl); }
}
document.addEventListener('keydown', handleKey);
document.addEventListener('dragover', (event) => event.preventDefault());
document.addEventListener('drop', (event) => { event.preventDefault(); void openFile(event.dataTransfer.files[0]); });
$('search-form').onsubmit = async (event) => {
  event.preventDefault(); const query = $('query').value.trim(); if (!book || !query) return;
  const token = ++searchGeneration, searchBook = book;
  $('search-button').disabled = true; $('results').replaceChildren(); $('search-status').textContent = '検索しています…';
  let count = 0;
  try {
    for (const section of searchBook.spine.spineItems) {
      if (token !== searchGeneration) return;
      await section.load(searchBook.load.bind(searchBook));
      try {
        if (token !== searchGeneration) return;
        for (const match of findBodyMatches(section, query, 500 - count)) {
          if (count >= 500) break;
          const li = document.createElement('li'), button = document.createElement('button');
          button.textContent = `章 ${section.index + 1}: ${match.excerpt}`;
          button.onclick = async () => { await navigate(match.cfi); rendition?.annotations.highlight(match.cfi, {}, undefined, 'search-match', { fill: '#ffd665', 'fill-opacity': '0.45' }); hideMobilePanel(); };
          li.append(button); $('results').append(li); count++;
        }
      } finally { section.unload(); }
      if (count >= 500) break;
    }
    if (token === searchGeneration) $('search-status').textContent = `${count}件${count === 500 ? '（上限500件）' : ''}`;
  } catch (error) { if (token === searchGeneration) $('search-status').textContent = `検索に失敗しました: ${error.message}`; }
  finally { if (token === searchGeneration) $('search-button').disabled = false; }
};
$('search-button').disabled = true;
$('sample').onclick = async () => {
  const zip = new JSZip();
  zip.file('mimetype', 'application/epub+zip');
  zip.file('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>');
  zip.file('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">bibi-html5-sample</dc:identifier><dc:title>読書の時間</dc:title><dc:creator>Bibi</dc:creator><dc:language>ja</dc:language><meta property="dcterms:modified">2026-10-07T00:00:00Z</meta></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="two.xhtml" media-type="application/xhtml+xml"/></manifest><spine page-progression-direction="rtl"><itemref idref="one"/><itemref idref="two"/></spine></package>');
  zip.file('nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>目次</title></head><body><nav epub:type="toc"><ol><li><a href="one.xhtml">第一章 朝の読書</a></li><li><a href="two.xhtml">第二章 夜の読書</a></li></ol></nav></body></html>');
  for (const [path, title, text] of [['one.xhtml','朝の読書','窓から朝の光が差し込む。本を開くと、新しい世界が始まる。'],['two.xhtml','夜の読書','静かな夜、読書の時間を楽しむ。しおりを挟んで、また明日。']]) zip.file(path, `<html xmlns="http://www.w3.org/1999/xhtml"><head><title>${title}</title><style>html{writing-mode:vertical-rl}body{font-family:serif;padding:24px;line-height:2}h1{font-size:1.6em}</style></head><body><h1>${title}</h1>${Array.from({length:12}, () => `<p>${text}</p>`).join('')}</body></html>`);
  zip.forEach((path, entry) => { entry.date = new Date('2026-10-07T00:00:00Z'); });
  await openFile(new File([await zip.generateAsync({type:'arraybuffer'})], 'sample.epub'));
};

