(function() {
    if (window._bibiDragInstalled) return;
    window._bibiDragInstalled = true;

    var drag = { active: false, x: 0, y: 0, sl: 0, st: 0 };

    function scrollElement() {
        return document.scrollingElement || document.documentElement;
    }

    function isZoomPanPage() {
        return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
    }

    function canDragPan() {
        var el = scrollElement();
        var canH = el.scrollWidth > el.clientWidth;
        var canV = el.scrollHeight > el.clientHeight;
        return isZoomPanPage() ? (canH || canV) : (canH && canV);
    }

    document.addEventListener('mousedown', function(e) {
        if (e.button !== 0 || !canDragPan()) return;
        var t = e.target;
        while (t && t !== document.body) {
            var tn = (t.tagName || '').toUpperCase();
            if (tn === 'A' || tn === 'INPUT' || tn === 'TEXTAREA' ||
                tn === 'SELECT' || tn === 'BUTTON') return;
            t = t.parentNode;
        }
        var el = scrollElement();
        drag.active = true;
        drag.x  = e.clientX;  drag.y  = e.clientY;
        drag.sl = el.scrollLeft; drag.st = el.scrollTop;
        el.style.cursor = 'grabbing';
        el.style.userSelect = 'none';
        e.preventDefault();
    }, true);

    document.addEventListener('mousemove', function(e) {
        if (drag.active) {
            var el = scrollElement();
            el.scrollLeft = drag.sl + (drag.x - e.clientX);
            el.scrollTop  = drag.st + (drag.y - e.clientY);
        } else {
            document.documentElement.style.cursor = canDragPan() ? 'grab' : '';
        }
    }, true);

    function endDrag() {
        if (!drag.active) return;
        drag.active = false;
        var el = scrollElement();
        el.style.cursor = canDragPan() ? 'grab' : '';
        el.style.userSelect = '';
    }
    document.addEventListener('mouseup',    endDrag, true);
    document.addEventListener('mouseleave', endDrag, true);
})();
