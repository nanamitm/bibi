// Parameter: %1=isRtl (0 or 1)
(function() {
    if (window._bibiKeyInstalled) return;
    window._bibiKeyInstalled = true;
    var STEP  = Math.round(window.innerWidth  * 0.5);
    var VSTEP = Math.round(window.innerHeight * 0.5);
    var isRtl = !!%1;
    var SIGN  = isRtl ? -1 : 1;
    var _busy = false;

    function navLeft() {
        _busy = true; console.log('epub-nav:left');
        setTimeout(function(){ _busy = false; }, 1000);
    }
    function navRight() {
        _busy = true; console.log('epub-nav:right');
        setTimeout(function(){ _busy = false; }, 1000);
    }
    function navUp() {
        _busy = true; console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
        setTimeout(function(){ _busy = false; }, 1000);
    }
    function navDown() {
        _busy = true; console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
        setTimeout(function(){ _busy = false; }, 1000);
    }
    function isVerticalPage() {
        var html = document.documentElement;
        var body = document.body;
        return (getComputedStyle(html).writingMode || '').indexOf('vertical') === 0 ||
               (body && (getComputedStyle(body).writingMode || '').indexOf('vertical') === 0);
    }
    function isZoomPanPage() {
        return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
    }

    window.addEventListener('keydown', function(e) {
        var left  = e.key === 'ArrowLeft';
        var right = e.key === 'ArrowRight';
        var up    = e.key === 'ArrowUp';
        var down  = e.key === 'ArrowDown';
        var pageUp = e.key === 'PageUp';
        var pageDown = e.key === 'PageDown';
        var space = e.key === ' ';
        if (!left && !right && !up && !down && !pageUp && !pageDown && !space) return;
        e.preventDefault();
        e.stopPropagation();
        if (_busy) return;

        var el   = document.documentElement;
        var maxH = Math.max(0, el.scrollWidth  - el.clientWidth);
        var maxV = Math.max(0, el.scrollHeight - el.clientHeight);
        var canH = maxH > 2;
        var canV = maxV > 2;

        if ((canH || canV) && isZoomPanPage()) {
            var absSl = Math.abs(el.scrollLeft);
            var atLeft = isRtl ? absSl >= maxH - 2 : absSl <= 2;
            var atRight = isRtl ? absSl <= 2 : absSl >= maxH - 2;
            var atTop = el.scrollTop <= 2;
            var atBot = el.scrollTop >= maxV - 2;
            if (left) {
                if (atLeft) navLeft(); else el.scrollLeft -= STEP;
            } else if (right) {
                if (atRight) navRight(); else el.scrollLeft += STEP;
            } else if (up || pageUp || (space && e.shiftKey)) {
                if (atTop) navUp(); else el.scrollTop -= VSTEP;
            } else if (down || pageDown || (space && !e.shiftKey)) {
                if (atBot) navDown(); else el.scrollTop += VSTEP;
            }
            if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
            return;
        }

        if (!isVerticalPage()) {
            var st = el.scrollTop;
            var atTop = st <= 2;
            var atBot = st >= maxV - 2;
            var backward = up || pageUp || (space && e.shiftKey);
            var forward = down || pageDown || (space && !e.shiftKey);
            if (left) {
                navLeft();
            } else if (right) {
                navRight();
            } else if (canV && backward) {
                if (atTop) navLeft();
                else window.scrollBy(0, -VSTEP);
            } else if (canV && forward) {
                if (atBot) navRight();
                else window.scrollBy(0, VSTEP);
            } else if (backward) {
                navLeft();
            } else if (forward) {
                navRight();
            }
            if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
            return;
        }

        if (canH && canV) {
            var dx = right || up ? STEP : left || down ? -STEP : 0;
            if (dx) window.scrollBy(dx, 0);
        } else if (canH) {
            var goRight = right || up;
            var goLeft  = left  || down;
            var absSl   = Math.abs(el.scrollLeft);
            var atLeft  = isRtl ? absSl >= maxH - 2 : absSl <= 2;
            var atRight = isRtl ? absSl <= 2        : absSl >= maxH - 2;
            if (goLeft)  { if (atLeft)  navLeft();  else window.scrollBy(-STEP, 0); }
            else         { if (atRight) navRight(); else window.scrollBy( STEP, 0); }
        } else if (canV) {
            if (up || down) {
                var st    = el.scrollTop;
                var atTop = st <= 2;
                var atBot = st >= maxV - 2;
                if (up)  { if (atTop) navUp();   else window.scrollBy(0, -VSTEP); }
                else     { if (atBot) navDown(); else window.scrollBy(0,  VSTEP); }
            } else {
                var st    = el.scrollTop;
                var atTop = st <= 2;
                var atBot = st >= maxV - 2;
                if (right) { if (atTop) navUp();   else window.scrollBy(0, -VSTEP); }
                else       { if (atBot) navDown(); else window.scrollBy(0,  VSTEP); }
            }
        } else {
            if (left)       navLeft();
            else if (right) navRight();
            else if (up)    navUp();
            else            navDown();
        }
    }, true);
})();
