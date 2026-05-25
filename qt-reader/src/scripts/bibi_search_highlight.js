// Parameters: %1=needle (JSON string), %2=occurrenceIndex, %3=scrollToMatch (true/false)
(function() {
    var needle = %1;
    var occurrenceIndex = %2;
    var scrollToMatch = %3;
    if (!needle) return false;

    var root = document.body || document.documentElement;
    var lower = needle.toLocaleLowerCase();

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
        document.head.appendChild(style);
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

    var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
        acceptNode: function(node) {
            if (node.parentElement &&
                node.parentElement.closest('script, style, mark[data-bibi-search-highlight="1"]'))
                return NodeFilter.FILTER_REJECT;
            return node.textContent.trim()
                ? NodeFilter.FILTER_ACCEPT
                : NodeFilter.FILTER_REJECT;
        }
    });

    var node;
    var seen = 0;
    var matches = [];
    while ((node = walker.nextNode())) {
        var text = node.textContent;
        var lowerText = text.toLocaleLowerCase();
        var index = lowerText.indexOf(lower);
        var positions = [];
        while (index >= 0) {
            positions.push({
                start: index,
                end: index + needle.length,
                occurrence: seen++
            });
            index = lowerText.indexOf(lower, index + needle.length);
        }
        if (positions.length)
            matches.push({ node: node, text: text, positions: positions });
    }

    var currentMark = null;
    var firstMark = null;
    matches.forEach(function(entry) {
        var frag = document.createDocumentFragment();
        var last = 0;
        entry.positions.forEach(function(match) {
            if (match.start > last)
                frag.appendChild(document.createTextNode(entry.text.slice(last, match.start)));

            var mark = document.createElement('mark');
            mark.dataset.bibiSearchHighlight = '1';
            mark.textContent = entry.text.slice(match.start, match.end);
            if (match.occurrence === occurrenceIndex) {
                mark.dataset.bibiSearchCurrent = '1';
                currentMark = mark;
            }
            if (!firstMark)
                firstMark = mark;
            frag.appendChild(mark);
            last = match.end;
        });
        if (last < entry.text.length)
            frag.appendChild(document.createTextNode(entry.text.slice(last)));
        if (entry.node.parentNode)
            entry.node.parentNode.replaceChild(frag, entry.node);
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
