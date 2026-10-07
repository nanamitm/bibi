import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';

const getPosition = readFileSync(new URL('../../qt-reader/src/scripts/bibi_get_reading_position.js', import.meta.url), 'utf8');
const restorePosition = readFileSync(new URL('../../qt-reader/src/scripts/bibi_scroll_to_position.js', import.meta.url), 'utf8');

// Exercise the exact scripts injected by Qt WebEngine in real Chromium, where
// vertical-rl and RTL scrollLeft are negative and a positive value clamps to 0.
for (const [target, writingMode, direction] of [
  ['html', 'vertical-rl', 'ltr'],
  ['body', 'vertical-rl', 'ltr'],
  ['html', 'vertical-lr', 'ltr'],
  ['html', 'horizontal-tb', 'rtl'],
  ['html', 'horizontal-tb', 'ltr'],
]) {
  test(`Qt reading position round trip: ${target} ${writingMode} ${direction}`, async ({page}) => {
    const horizontal = writingMode === 'horizontal-tb';
    const style = `<style>${target}{writing-mode:${writingMode};direction:${direction}}${horizontal ? 'body{width:2000px;height:100px}' : ''}</style>`;
    await page.setContent(`<!doctype html><html><head>${style}</head><body>${'<p>読書位置のテストです。</p>'.repeat(horizontal ? 1 : 80)}</body></html>`);
    const result = await page.evaluate(({getPosition, restorePosition}) => {
      const el = document.documentElement;
      const max = el.scrollWidth - el.clientWidth;
      // Find the browser's actual scroll direction independently of the script.
      el.scrollLeft = -max / 2;
      if (el.scrollLeft === 0) el.scrollLeft = max / 2;
      const initial = el.scrollLeft;
      const saved = (0, eval)(getPosition);
      el.scrollLeft = 0;
      (0, eval)(restorePosition.replaceAll('%1', String(saved)));
      return {max, initial, saved, restored:el.scrollLeft};
    }, {getPosition, restorePosition});
    expect(result.max).toBeGreaterThan(0);
    expect(result.saved).toBeCloseTo(0.5, 2);
    expect(result.restored).toBe(result.initial);
  });
}

test('Qt reading position restores vertical scrolling for horizontal text', async ({page}) => {
  await page.setContent(`<!doctype html><html><body>${'<p>横書きの本文です。</p>'.repeat(100)}</body></html>`);
  const position = await page.evaluate(({getPosition, restorePosition}) => {
    const el = document.documentElement;
    el.scrollTop = (el.scrollHeight - el.clientHeight) / 2;
    const saved = (0, eval)(getPosition);
    el.scrollTop = 0;
    (0, eval)(restorePosition.replaceAll('%1', String(saved)));
    return (0, eval)(getPosition);
  }, {getPosition, restorePosition});
  expect(position).toBeCloseTo(0.5, 2);
});
