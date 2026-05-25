// Parameter: %1=zoomFactor
(function() {
    var zoom = %1;
    var html = document.documentElement;
    var body = document.body || html;
    window._bibiCssZoom = zoom;
    var images = Array.from(document.images);
    var svgElements = Array.from(document.querySelectorAll('svg'));
    var svgImages = Array.from(document.querySelectorAll('svg image'));
    var imageLikeElements = images.concat(svgElements);
    if (!imageLikeElements.length)
        imageLikeElements = Array.from(document.querySelectorAll('object'));
    var scrollEl = document.scrollingElement || html;
    var prevCanH = scrollEl.scrollWidth > scrollEl.clientWidth;
    var prevMaxH = Math.max(1, scrollEl.scrollWidth - scrollEl.clientWidth);
    var prevScrollRatio = Math.abs(scrollEl.scrollLeft) / prevMaxH;

    function resetImageZoom() {
        body.style.width = '';
        body.style.height = '';
        body.style.minWidth = '';
        body.style.minHeight = '';
        imageLikeElements.forEach(function(el) {
            if (el.dataset && el.dataset.bibiImageZoomOnly === '1') {
                el.style.width = '';
                el.style.height = '';
                el.style.maxWidth = '';
                el.style.maxHeight = '';
                el.style.transform = '';
                el.style.transformOrigin = '';
                el.style.display = '';
                el.style.willChange = '';
                el.style.overflow = '';
            }
            var node = el.parentElement;
            while (node && node !== body) {
                if (node.dataset && node.dataset.bibiZoomContainer === '1') {
                    node.style.overflow = '';
                    node.style.maxWidth = '';
                    node.style.maxHeight = '';
                    node.style.width = '';
                    node.style.height = '';
                    node.style.minWidth = '';
                    node.style.minHeight = '';
                }
                node = node.parentElement;
            }
        });
    }

    function isIgnorableElement(el) {
        var tag = (el.tagName || '').toUpperCase();
        return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||
               tag === 'META' || tag === 'TITLE';
    }

    function isImageOnlyPage() {
        if (!imageLikeElements.length && !svgImages.length) return false;
        if ((body.innerText || '').trim().length > 0) return false;
        var meaningful = Array.from(body.children).filter(function(el) {
            return !isIgnorableElement(el);
        });
        if (!meaningful.length) return false;
        return meaningful.every(function(el) {
            var tag = (el.tagName || '').toUpperCase();
            if (tag === 'IMG' || tag === 'SVG' || tag === 'OBJECT' || tag === 'PICTURE') return true;
            return el.querySelector && el.querySelector('img, svg, object, picture, svg image');
        });
    }

    function numericAttr(el, name) {
        var value = el.getAttribute && el.getAttribute(name);
        if (!value) return 0;
        var n = parseFloat(String(value).replace(/px$/i, ''));
        return isFinite(n) ? n : 0;
    }

    function viewBoxSize(el) {
        var box = el.viewBox && el.viewBox.baseVal;
        if (box && box.width > 0 && box.height > 0)
            return { width: box.width, height: box.height };
        var attr = el.getAttribute && el.getAttribute('viewBox');
        if (!attr) return null;
        var parts = attr.trim().split(/[\s,]+/).map(parseFloat);
        if (parts.length === 4 && parts[2] > 0 && parts[3] > 0)
            return { width: parts[2], height: parts[3] };
        return null;
    }

    function measuredBaseSize(el) {
        var rect = el.getBoundingClientRect();
        var attrWidth = numericAttr(el, 'width');
        var attrHeight = numericAttr(el, 'height');
        var box = viewBoxSize(el);
        var width = 0;
        var height = 0;
        if (el.naturalWidth > 0 && el.naturalHeight > 0) {
            width = el.naturalWidth;
            height = el.naturalHeight;
        } else if (box) {
            width = box.width;
            height = box.height;
        } else if (attrWidth > 0 && attrHeight > 0) {
            width = attrWidth;
            height = attrHeight;
        } else if (rect.width > 1 && rect.height > 1) {
            width = rect.width;
            height = rect.height;
        }
        if (width > 0 && height <= 0 && rect.width > 1 && rect.height > 1)
            height = width * rect.height / rect.width;
        if (height > 0 && width <= 0 && rect.width > 1 && rect.height > 1)
            width = height * rect.width / rect.height;
        if (!isFinite(width) || !isFinite(height) || width <= 1 || height <= 1)
            return null;
        return { width: width, height: height };
    }

    function useTransformZoom(el) {
        return false;
    }

    function rememberBaseSize(el) {
        if (!el.dataset) return false;
        if (parseFloat(el.dataset.bibiBaseWidth) > 1 &&
            parseFloat(el.dataset.bibiBaseHeight) > 1)
            return true;
        var size = measuredBaseSize(el);
        if (!size) return false;
        el.dataset.bibiBaseWidth = String(size.width);
        el.dataset.bibiBaseHeight = String(size.height);
        return true;
    }

    function applyImageZoom() {
        body.style.zoom = '';
        body.style.width = 'max-content';
        body.style.height = 'auto';
        body.style.minWidth = '100%';
        body.style.minHeight = '100%';

        var allReady = true;
        imageLikeElements.forEach(function(el) {
            if (!el.dataset) return;
            if (!rememberBaseSize(el)) {
                allReady = false;
                return;
            }
            var baseWidth = parseFloat(el.dataset.bibiBaseWidth) || el.naturalWidth || 1;
            var baseHeight = parseFloat(el.dataset.bibiBaseHeight) || el.naturalHeight || 1;
            var scaledWidth = baseWidth * zoom;
            var scaledHeight = baseHeight * zoom;

            el.dataset.bibiImageZoomOnly = '1';
            el.style.maxWidth = 'none';
            el.style.maxHeight = 'none';
            el.style.width = scaledWidth + 'px';
            el.style.height = scaledHeight + 'px';
            el.style.display = 'block';
            el.style.overflow = 'visible';
            if (useTransformZoom(el)) {
                el.style.transformOrigin = '0 0';
                el.style.transform = 'scale(' + zoom + ')';
                el.style.willChange = 'transform';
            } else {
                el.style.width = scaledWidth + 'px';
                el.style.height = scaledHeight + 'px';
                el.style.transform = '';
                el.style.transformOrigin = '';
                el.style.willChange = '';
            }

            var immediateParent = el.parentElement;
            if (!immediateParent || immediateParent === body) {
                body.style.width = Math.max(body.scrollWidth, scaledWidth) + 'px';
                body.style.height = Math.max(body.scrollHeight, scaledHeight) + 'px';
            } else {
                immediateParent.dataset.bibiZoomContainer = '1';
                immediateParent.style.overflow = 'visible';
                immediateParent.style.maxWidth = 'none';
                immediateParent.style.maxHeight = 'none';
                immediateParent.style.width = scaledWidth + 'px';
                immediateParent.style.height = scaledHeight + 'px';
                immediateParent.style.minWidth = scaledWidth + 'px';
                immediateParent.style.minHeight = scaledHeight + 'px';
            }

            var node = immediateParent ? immediateParent.parentElement : null;
            while (node && node !== body) {
                node.dataset.bibiZoomContainer = '1';
                node.style.overflow = 'visible';
                node.style.maxWidth = 'none';
                node.style.maxHeight = 'none';
                node.style.width = 'max-content';
                node.style.height = 'auto';
                node = node.parentElement;
            }
        });
        return allReady;
    }

    var imageOnly = isImageOnlyPage();
    resetImageZoom();
    if (zoom === 1) {
        body.style.zoom = '';
        html.style.overflow = '';
        body.style.overflow = '';
    } else {
        html.style.overflow = 'auto';
        body.style.overflow = 'auto';
        if (imageOnly) {
            var ready = applyImageZoom();
            if (!ready) {
                requestAnimationFrame(function() {
                    applyImageZoom();
                    if (window._bibiAlignImageOnlyInitialRightEnabled && window._bibiAlignImageOnlyInitialRight)
                        window._bibiAlignImageOnlyInitialRight();
                    else if (window._bibiAlignImageOnlyInitialLeftEnabled && window._bibiAlignImageOnlyInitialLeft)
                        window._bibiAlignImageOnlyInitialLeft();
                    if (window._bibiReportReadingPosition)
                        window._bibiReportReadingPosition();
                });
            }
        } else {
            body.style.zoom = zoom;
        }
    }
    if (window._bibiApplyReadingInsets) window._bibiApplyReadingInsets();
    requestAnimationFrame(function() {
        if (!imageOnly) return;
        var el = document.scrollingElement || document.documentElement;
        if (el.scrollWidth <= el.clientWidth) return;
        var maxH = Math.max(0, el.scrollWidth - el.clientWidth);
        if (window._bibiAlignImageOnlyInitialLeftEnabled && window._bibiAlignImageOnlyInitialLeft)
            window._bibiAlignImageOnlyInitialLeft();
        else if (window._bibiAlignImageOnlyInitialRightEnabled && window._bibiAlignImageOnlyInitialRight)
            window._bibiAlignImageOnlyInitialRight();
        else if (window._bibiAlignImageOnlyInitialLeftEnabled)
            el.scrollLeft = -maxH;
        else if (window._bibiAlignImageOnlyInitialRightEnabled)
            el.scrollLeft = maxH;
        else if (prevCanH)
            el.scrollLeft = prevScrollRatio * maxH;
        if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
    });
})();
