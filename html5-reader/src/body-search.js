// Search the body without changing the DOM: CFI ranges must refer to the same
// nodes that epub.js will subsequently render.
const excluded = 'script,style,noscript,template,rt,rp,svg,img,object,picture,video,audio,iframe,input,select,textarea,[hidden],[aria-hidden="true"]';

export function findBodyMatches(section, query, limit = 500) {
  const doc = section.document;
  const body = doc.querySelector('body');
  if (!body || !query || limit <= 0) return [];
  const walker = doc.createTreeWalker(body, NodeFilter.SHOW_TEXT, {
    acceptNode(node) {
      if (node.parentElement.closest(excluded)) return NodeFilter.FILTER_REJECT;
      for (let parent = node.parentElement; parent; parent = parent.parentElement) {
        if (parent.style?.display === 'none' || ['hidden', 'collapse'].includes(parent.style?.visibility))
          return NodeFilter.FILTER_REJECT;
        if (parent === body) break;
      }
      return NodeFilter.FILTER_ACCEPT;
    },
  });
  const pattern = new RegExp(query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
  const matches = [];
  for (let node = walker.nextNode(); node && matches.length < limit; node = walker.nextNode()) {
    pattern.lastIndex = 0;
    for (let match; matches.length < limit && (match = pattern.exec(node.textContent));) {
      const range = doc.createRange();
      range.setStart(node, match.index);
      range.setEnd(node, match.index + match[0].length);
      const start = Math.max(0, match.index - 75);
      const end = Math.min(node.length, match.index + match[0].length + 75);
      matches.push({
        cfi: section.cfiFromRange(range),
        excerpt: `${start ? '…' : ''}${node.textContent.slice(start, end)}${end < node.length ? '…' : ''}`,
      });
      // Keep overlapping literal matches, like the previous epub.js search.
      pattern.lastIndex = match.index + 1;
    }
  }
  return matches;
}
