(function() {
    window._bibiApplyReadingInsets = function() {
        var html = document.documentElement;
        var body = document.body || html;
        var images = Array.from(document.images);

        function hasText(node) {
            return !!node && (node.innerText || '').trim().length > 0;
        }

        function isIgnorableElement(el) {
            var tag = (el.tagName || '').toUpperCase();
            return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||
                   tag === 'META' || tag === 'TITLE';
        }

        function isImageOnlyPage() {
            if (!images.length || hasText(body)) return false;
            var meaningful = Array.from(body.children).filter(function(el) {
                return !isIgnorableElement(el);
            });
            return meaningful.length > 0 && meaningful.every(function(el) {
                if ((el.tagName || '').toUpperCase() === 'IMG') return true;
                return el.querySelector && el.querySelector('img');
            });
        }

        function isVerticalWriting(el) {
            if (!el) return false;
            return (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0;
        }

        function findTextElement() {
            var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, {
                acceptNode: function(node) {
                    return node.textContent.trim()
                        ? NodeFilter.FILTER_ACCEPT
                        : NodeFilter.FILTER_REJECT;
                }
            });
            var node = walker.nextNode();
            return node ? node.parentElement : null;
        }

        function resetInsetTargets() {
            Array.from(document.querySelectorAll('[data-bibi-reading-inset="1"]'))
                .forEach(function(el) {
                    el.style.boxSizing = '';
                    el.style.height = '';
                    el.style.minHeight = '';
                    el.style.marginTop = '';
                    el.style.marginBottom = '';
                    el.style.paddingTop = '';
                    el.style.paddingBottom = '';
                    delete el.dataset.bibiReadingInset;
                });
        }

        function findReadingInsetTarget(textEl) {
            var target = null;
            var node = textEl;
            while (node && node !== body && node !== html) {
                if (isVerticalWriting(node))
                    target = node;
                node = node.parentElement;
            }
            return target;
        }

        function applyBodyInset() {
            var zoom = Math.max(0.1, Number(window._bibiCssZoom || 1));
            var viewportHeight = (100 / zoom).toFixed(4) + 'vh';
            body.dataset.bibiReadingInset = '1';
            body.style.boxSizing = 'border-box';
            body.style.height = 'calc(' + viewportHeight + ' - 3em)';
            body.style.minHeight = '';
            body.style.paddingTop = '';
            body.style.paddingBottom = '';
            body.style.marginTop = '1em';
            body.style.marginBottom = '2em';
        }

        function applyContentInsetTarget(el) {
            if (!el || el === body) return;
            el.dataset.bibiReadingInset = '1';
            el.style.boxSizing = 'border-box';
            el.style.height = '100%';
            el.style.minHeight = '';
            el.style.paddingTop = '';
            el.style.paddingBottom = '';
            el.style.marginTop = '';
            el.style.marginBottom = '';
        }

        resetInsetTargets();

        var textEl = findTextElement();
        var vertical = isVerticalWriting(html) ||
                       isVerticalWriting(body) ||
                       isVerticalWriting(textEl);

        if (vertical && !isImageOnlyPage()) {
            applyBodyInset();
            applyContentInsetTarget(findReadingInsetTarget(textEl));
            html.style.scrollPaddingTop = '1em';
            html.style.scrollPaddingBottom = '2em';
        } else {
            html.style.scrollPaddingTop = '';
            html.style.scrollPaddingBottom = '';
        }
    };
    window._bibiApplyReadingInsets();
})();
