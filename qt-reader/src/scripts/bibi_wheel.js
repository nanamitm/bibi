// Parameter: %1=SIGN (1=LTR, -1=RTL)
(function() {
    if (window._bibiWheelInstalled) return;
    window._bibiWheelInstalled = true;
    var SIGN  = %1;
    var _busy = false;
    function isVerticalPage() {
        var html = document.documentElement;
        var body = document.body;
        return (getComputedStyle(html).writingMode || '').indexOf('vertical') === 0 ||
               (body && (getComputedStyle(body).writingMode || '').indexOf('vertical') === 0);
    }
    function isZoomPanPage() {
        return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
    }
    window.addEventListener('wheel', function(e) {
        if (e.ctrlKey) { e.preventDefault(); return; }
        if (e.deltaX === 0 && e.deltaY === 0) return;
        e.preventDefault();
        var el    = document.documentElement;
        var canH  = el.scrollWidth  > el.clientWidth;
        var canV  = el.scrollHeight > el.clientHeight;
        var isRtl = SIGN < 0;
        if (isZoomPanPage() && (canH || canV)) {
            var maxH = Math.max(0, el.scrollWidth  - el.clientWidth);
            var maxV = Math.max(0, el.scrollHeight - el.clientHeight);
            var absSl = Math.abs(el.scrollLeft);
            var atLeft = isRtl ? absSl >= maxH - 2 : absSl <= 2;
            var atRight = isRtl ? absSl <= 2 : absSl >= maxH - 2;
            var atTop = el.scrollTop <= 2;
            var atBottom = el.scrollTop >= maxV - 2;
            var dx = canH ? (e.deltaX || (!canV ? SIGN * e.deltaY : 0)) : 0;
            var dy = canV ? e.deltaY : 0;
            if (!_busy && dy < 0 && atTop) {
                _busy = true; console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
                setTimeout(function(){ _busy=false; }, 1000);
            } else if (!_busy && dy > 0 && atBottom) {
                _busy = true; console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
                setTimeout(function(){ _busy=false; }, 1000);
            } else if (!_busy && dx < 0 && atLeft) {
                _busy = true; console.log('epub-nav:left');
                setTimeout(function(){ _busy=false; }, 1000);
            } else if (!_busy && dx > 0 && atRight) {
                _busy = true; console.log('epub-nav:right');
                setTimeout(function(){ _busy=false; }, 1000);
            } else {
                el.scrollLeft += dx;
                el.scrollTop  += dy;
            }
        } else if (canH && canV) {
            if (isVerticalPage()) {
                var maxH = Math.max(0, el.scrollWidth - el.clientWidth);
                var absSl = Math.abs(el.scrollLeft);
                var dx = SIGN * e.deltaY;
                var atLeft = isRtl ? absSl >= maxH - 2 : absSl <= 2;
                var atRight = isRtl ? absSl <= 2 : absSl >= maxH - 2;
                if (!_busy && dx < 0 && atLeft) {
                    _busy = true;
                    console.log('epub-nav:left');
                    setTimeout(function(){ _busy=false; }, 1000);
                } else if (!_busy && dx > 0 && atRight) {
                    _busy = true;
                    console.log('epub-nav:right');
                    setTimeout(function(){ _busy=false; }, 1000);
                } else {
                    window.scrollBy(dx, 0);
                }
            } else {
                window.scrollBy(e.deltaX || 0, e.deltaY);
            }
        } else if (canH) {
            var sl    = el.scrollLeft;
            var max   = Math.max(0, el.scrollWidth - el.clientWidth);
            var absSl = Math.abs(sl);
            var dx    = SIGN * e.deltaY;
            var atLeft  = isRtl ? absSl >= max - 2 : absSl <= 2;
            var atRight = isRtl ? absSl <= 2        : absSl >= max - 2;
            if (!_busy && dx < 0 && atLeft) {
                _busy = true;
                console.log('epub-nav:left');
                setTimeout(function(){ _busy=false; }, 1000);
            } else if (!_busy && dx > 0 && atRight) {
                _busy = true;
                console.log('epub-nav:right');
                setTimeout(function(){ _busy=false; }, 1000);
            } else {
                window.scrollBy(dx, 0);
            }
        } else if (canV) {
            var st    = el.scrollTop;
            var maxV  = Math.max(0, el.scrollHeight - el.clientHeight);
            var atTop    = st <= 2;
            var atBottom = st >= maxV - 2;
            if (!_busy && e.deltaY < 0 && atTop) {
                _busy = true;
                console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
                setTimeout(function(){ _busy=false; }, 1000);
            } else if (!_busy && e.deltaY > 0 && atBottom) {
                _busy = true;
                console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
                setTimeout(function(){ _busy=false; }, 1000);
            } else {
                window.scrollBy(0, e.deltaY);
            }
        } else {
            if (!_busy) {
                _busy = true;
                console.log(SIGN * e.deltaY > 0 ? 'epub-nav:right' : 'epub-nav:left');
                setTimeout(function(){ _busy=false; }, 1000);
            }
        }
    }, { passive: false });
})();
