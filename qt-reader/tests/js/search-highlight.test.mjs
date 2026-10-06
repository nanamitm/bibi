// Runs src/scripts/bibi_search_highlight.js against the shared XHTML fixtures
// in a DOM (jsdom, parsed as application/xhtml+xml like the reader does) and
// holds its text extraction to the same expected files as EpubReader's C++
// tests (tests/tst_epubreader.cpp).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { JSDOM } from 'jsdom';

const here = dirname(fileURLToPath(import.meta.url));
const fixtureDir = join(here, '..', 'fixtures', 'search');
const scriptPath = join(here, '..', '..', 'src', 'scripts', 'bibi_search_highlight.js');

const source = readFileSync(scriptPath, 'utf8');
const BUILD_MARKER = 'var built = buildSearchText(root);';

function readFixture(name) {
    return readFileSync(join(fixtureDir, name), 'utf8');
}

function expectedText(xhtmlName) {
    return readFixture(xhtmlName.replace(/\.xhtml$/, '.txt')).replace(/[\r\n]+$/, '');
}

// Loads a fixture and returns a function running the highlight script in it,
// substituting the parameter exactly like MainWindow::jumpToSearchResult().
function open(xhtmlName) {
    const dom = new JSDOM(readFixture(xhtmlName), {
        contentType: 'application/xhtml+xml',
        runScripts: 'outside-only',
        pretendToBeVisual: true,
    });
    const instrumented = source.replace(BUILD_MARKER,
        BUILD_MARKER + ' window.__bibiSearchText = built.text;');
    const run = (params) => dom.window.eval(instrumented.replace('%1', JSON.stringify(params)));
    const marks = (selector = 'mark[data-bibi-search-highlight="1"]') =>
        Array.from(dom.window.document.querySelectorAll(selector));
    return { dom, window: dom.window, run, marks };
}

function offsetsOf(text, needle) {
    const out = [];
    for (let p = text.indexOf(needle); p >= 0; p = text.indexOf(needle, p + needle.length))
        out.push([p, needle.length]);
    return out;
}

test('the script has exactly one placeholder, filled by a single QString::arg()', () => {
    assert.equal(source.split('%1').length, 2);
    assert.doesNotMatch(source, /%[2-9]/);
    assert.ok(source.includes(BUILD_MARKER), 'instrumentation marker is missing');
});

const fixtures = readdirSync(fixtureDir).filter((name) => name.endsWith('.xhtml')).sort();
assert.ok(fixtures.length > 0, 'no fixtures found');

for (const name of fixtures) {
    test(`DOM text matches the C++ expectation: ${name}`, (t) => {
        const page = open(name);
        t.after(() => page.window.close());
        page.run({ needle: 'no-such-text-in-any-fixture', current: 0, scroll: false, matches: [] });
        assert.equal(page.window.__bibiSearchText, expectedText(name));
    });
}

test('a match split by inline markup is highlighted piece by piece', (t) => {
    const page = open('inline-markup.xhtml');
    t.after(() => page.window.close());
    const text = expectedText('inline-markup.xhtml');

    page.run({ needle: '東京', current: 0, scroll: false, matches: offsetsOf(text, '東京') });
    const marks = page.marks();
    assert.deepEqual(marks.map((m) => m.textContent), ['東', '京']);
    assert.equal(marks[1].parentNode.localName, 'em');
    assert.ok(marks.every((m) => m.getAttribute('data-bibi-search-current') === '1'));
});

test('ruby readings are not highlighted and "current" selects the right match', (t) => {
    const page = open('inline-markup.xhtml');
    t.after(() => page.window.close());
    const matches = offsetsOf(expectedText('inline-markup.xhtml'), '漢字');
    assert.equal(matches.length, 2);

    page.run({ needle: '漢字', current: 1, scroll: false, matches });
    assert.equal(page.marks().map((m) => m.textContent).join(''), '漢字漢字');
    assert.ok(page.marks().every((m) => m.closest('rt') === null));
    const current = page.marks('mark[data-bibi-search-current="1"]');
    assert.deepEqual(current.map((m) => m.textContent), ['漢字']);
    assert.ok(current[0].closest('ruby').textContent.includes('かんじ'));
});

test('matches across block boundaries and collapsed whitespace', (t) => {
    const page = open('blocks-and-whitespace.xhtml');
    t.after(() => page.window.close());
    const text = expectedText('blocks-and-whitespace.xhtml');

    page.run({ needle: 'セル1 セル2', current: 0, scroll: false, matches: offsetsOf(text, 'セル1 セル2') });
    assert.deepEqual(page.marks().map((m) => m.textContent), ['セル1', 'セル2']);

    page.run({ needle: '前後の空白 と', current: 0, scroll: false, matches: offsetsOf(text, '前後の空白 と') });
    assert.equal(page.marks().length, 1);
    assert.equal(page.marks()[0].textContent.replace(/\s+/g, ' '), '前後の空白 と');
});

test('offsets that do not fit the DOM fall back to searching the text', (t) => {
    const page = open('inline-markup.xhtml');
    t.after(() => page.window.close());

    page.run({ needle: '漢字', current: 0, scroll: false, matches: [[0, 2], [3, 2]] });
    assert.equal(page.marks().map((m) => m.textContent).join(''), '漢字漢字');
});

test('running again replaces the previous highlights and keeps the text intact', (t) => {
    const page = open('inline-markup.xhtml');
    t.after(() => page.window.close());
    const text = expectedText('inline-markup.xhtml');

    page.run({ needle: '東京', current: 0, scroll: false, matches: offsetsOf(text, '東京') });
    page.run({ needle: 'リンク', current: 0, scroll: false, matches: offsetsOf(text, 'リンク') });
    assert.equal(page.marks().map((m) => m.textContent).join(''), 'リンク');
    assert.equal(page.window.__bibiSearchText, text);
});
