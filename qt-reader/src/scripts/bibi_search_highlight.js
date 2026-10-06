// Parameter (substituted for the placeholder in `var params` below), a JSON object:
//   {"needle": string, "current": int, "scroll": bool, "matches": [[start, length], ...]}
// matches are offsets into the normalized chapter text produced by
// EpubReader::searchableText(). buildSearchText() below must follow exactly
// the same rules so that those offsets can be mapped back onto text nodes.
(function() {
    var params = %1;
    var needle = String(params.needle || '').replace(/[ \t\n\f\r\u00a0]+/g, ' ').replace(/^ | $/g, '');
    var occurrenceIndex = params.current | 0;
    var scrollToMatch = !!params.scroll;
    if (!needle) return false;

    var root = document.body || document.documentElement;

    Array.from(document.querySelectorAll('mark[data-bibi-search-highlight="1"]'))
        .forEach(function(mark) {
            var parent = mark.parentNode;
            if (!parent) return;
            while (mark.firstChild)
                parent.insertBefore(mark.firstChild, mark);
            parent.removeChild(mark);
            parent.normalize();
        });

    var style = document.getElementById('bibi-search-highlight-style');
    if (!style) {
        style = document.createElement('style');
        (document.head || root).appendChild(style);
    }
    style.id = 'bibi-search-highlight-style';
    style.textContent =
        'mark[data-bibi-search-highlight="1"] {' +
        '  background: rgba(255, 230, 109, 0.42);' +
        '  color: inherit;' +
        '  padding: 0 0.08em;' +
        '  border-radius: 0.12em;' +
        '}' +
        'mark[data-bibi-search-current="1"] {' +
        '  background: #ffb000;' +
        '  box-shadow: 0 0 0 1px rgba(80, 52, 0, 0.28);' +
        '}';

    // ── Same rules as EpubReader::searchableText() ──────────────────────
    var SKIPPED = {
        script: 1, style: 1, head: 1, title: 1, meta: 1, link: 1, noscript: 1, template: 1,
        rt: 1, rp: 1, svg: 1, img: 1, object: 1, picture: 1, video: 1, audio: 1, iframe: 1
    };
    var BLOCK = {
        address: 1, article: 1, aside: 1, blockquote: 1, body: 1, br: 1, caption: 1,
        dd: 1, div: 1, dl: 1, dt: 1, figcaption: 1, figure: 1, footer: 1, h1: 1, h2: 1,
        h3: 1, h4: 1, h5: 1, h6: 1, header: 1, hr: 1, li: 1, main: 1, nav: 1, ol: 1, p: 1,
        pre: 1, section: 1, table: 1, td: 1, th: 1, tr: 1, ul: 1
    };

    function localTagName(el) {
        var name = String(el.localName || el.nodeName || '').toLowerCase();
        var colon = name.indexOf(':');
        return colon >= 0 ? name.slice(colon + 1) : name;
    }

    function isSearchWhitespace(code) {
        return code === 0x20 || code === 0x09 || code === 0x0A ||
               code === 0x0C || code === 0x0D || code === 0xA0;
    }

    // Returns the normalized text plus, for every character of it, the text
    // node (index into textNodes, -1 for a block separator) and offset it
    // came from.
    function buildSearchText(start) {
        var chars = [], nodeIndex = [], offsets = [], textNodes = [];
        var pending = false, pendingNode = -1, pendingOffset = 0;

        function push(ch, node, offset) {
            if (isSearchWhitespace(ch.charCodeAt(0))) {
                if (!pending && chars.length) {
                    pending = true;
                    pendingNode = node;
                    pendingOffset = offset;
                }
                return;
            }
            if (pending) {
                chars.push(' ');
                nodeIndex.push(pendingNode);
                offsets.push(pendingOffset);
                pending = false;
            }
            chars.push(ch);
            nodeIndex.push(node);
            offsets.push(offset);
        }

        function walk(parent) {
            for (var child = parent.firstChild; child; child = child.nextSibling) {
                if (child.nodeType === 3 || child.nodeType === 4) {
                    var index = textNodes.length;
                    textNodes.push(child);
                    var data = child.data;
                    for (var i = 0; i < data.length; ++i)
                        push(data.charAt(i), index, i);
                } else if (child.nodeType === 1) {
                    var tag = localTagName(child);
                    if (SKIPPED[tag]) continue;
                    var block = BLOCK[tag];
                    if (block) push('\n', -1, 0);
                    walk(child);
                    if (block) push('\n', -1, 0);
                }
            }
        }

        walk(start);
        return { text: chars.join(''), nodeIndex: nodeIndex, offsets: offsets, textNodes: textNodes };
    }
    var built = buildSearchText(root);
    var text = built.text;
    var lowerNeedle = needle.toLowerCase();

    function fitsText(m) {
        return Array.isArray(m) && m[0] >= 0 && m[1] > 0 && m[0] + m[1] <= text.length &&
               text.substr(m[0], m[1]).toLowerCase() === lowerNeedle;
    }

    var matches = Array.isArray(params.matches) ? params.matches : [];
    if (!matches.length || !matches.every(fitsText)) {
        // The offsets do not fit this DOM (e.g. the chapter was not well-formed
        // XML and was extracted by the fallback); search the text here instead.
        matches = [];
        var lowerText = text.toLowerCase();
        if (lowerText.length === text.length) {
            for (var p = lowerText.indexOf(lowerNeedle); p >= 0;
                 p = lowerText.indexOf(lowerNeedle, p + lowerNeedle.length))
                matches.push([p, lowerNeedle.length]);
        }
    }

    // Split every match into per-text-node pieces.
    var pieces = [];
    matches.forEach(function(m, matchIndex) {
        var current = null;
        for (var i = m[0]; i < m[0] + m[1]; ++i) {
            var node = built.nodeIndex[i];
            if (node < 0) continue;
            var offset = built.offsets[i];
            if (current && current.node === node) {
                current.end = offset + 1;
            } else {
                current = { node: node, start: offset, end: offset + 1, match: matchIndex };
                pieces.push(current);
            }
        }
    });

    // Wrap from the end of the document backwards so earlier offsets stay valid.
    pieces.sort(function(a, b) { return (b.node - a.node) || (b.start - a.start); });

    var currentMark = null;
    var firstMark = null;
    pieces.forEach(function(piece) {
        var node = built.textNodes[piece.node];
        if (!node.parentNode) return;
        if (piece.end < node.data.length)
            node.splitText(piece.end);
        var target = piece.start > 0 ? node.splitText(piece.start) : node;

        var mark = document.createElement('mark');
        mark.setAttribute('data-bibi-search-highlight', '1');
        if (piece.match === occurrenceIndex) {
            mark.setAttribute('data-bibi-search-current', '1');
            currentMark = mark; // ends up as the first piece in document order
        }
        firstMark = mark;
        target.parentNode.insertBefore(mark, target);
        mark.appendChild(target);
    });

    var mark = currentMark || firstMark;
    if (!mark) return false;

    var selection = window.getSelection();
    selection.removeAllRanges();

    function scrollMatchIntoViewIfNeeded() {
        requestAnimationFrame(function() {
            var el = document.scrollingElement || document.documentElement;
            var rect = mark.getBoundingClientRect();
            if (!rect || (!rect.width && !rect.height)) {
                if (scrollToMatch)
                    mark.scrollIntoView({ block: 'center', inline: 'center' });
                return;
            }

            var margin = Math.max(24, Math.round(Math.min(el.clientWidth, el.clientHeight) * 0.08));
            var dx = 0;
            var dy = 0;

            if (scrollToMatch) {
                dx = rect.left + rect.width / 2 - el.clientWidth / 2;
                dy = rect.top + rect.height / 2 - el.clientHeight / 2;
            } else {
                if (rect.left < margin)
                    dx = rect.left - margin;
                else if (rect.right > el.clientWidth - margin)
                    dx = rect.right - (el.clientWidth - margin);

                if (rect.top < margin)
                    dy = rect.top - margin;
                else if (rect.bottom > el.clientHeight - margin)
                    dy = rect.bottom - (el.clientHeight - margin);
            }

            if (dx || dy)
                el.scrollBy(dx, dy);

            requestAnimationFrame(function() {
                var after = mark.getBoundingClientRect();
                if (scrollToMatch && after && (after.width || after.height)) {
                    var adjustX = after.left + after.width / 2 - el.clientWidth / 2;
                    var adjustY = after.top + after.height / 2 - el.clientHeight / 2;
                    if (Math.abs(adjustX) > 4 || Math.abs(adjustY) > 4)
                        el.scrollBy(adjustX, adjustY);
                }
                if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
            });
        });
    }
    scrollMatchIntoViewIfNeeded();
    return true;
})();
