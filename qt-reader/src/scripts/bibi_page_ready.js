// Parameters: %1=isRtl, %2=scrollToEnd, %3=alignImageOnlyInitialRight, %4=alignImageOnlyInitialLeft
(function(){
  var imgs = Array.from(document.querySelectorAll('img, svg image'));
  var ps = imgs.map(function(img){
    return img.decode ? img.decode().catch(function(){}) : Promise.resolve();
  });
  function isIgnorableElement(el) {
    var tag = (el.tagName || '').toUpperCase();
    return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||
           tag === 'META' || tag === 'TITLE';
  }
  function isImageOnlyPage() {
    var body = document.body || document.documentElement;
    var imageLike = document.querySelector('img, svg, object, picture, svg image');
    if (!imageLike || (body.innerText || '').trim().length > 0) return false;
    var meaningful = Array.from(body.children).filter(function(el){
      return !isIgnorableElement(el);
    });
    return meaningful.length > 0 && meaningful.every(function(el){
      var tag = (el.tagName || '').toUpperCase();
      if (tag === 'IMG' || tag === 'SVG' || tag === 'OBJECT' || tag === 'PICTURE') return true;
      return el.querySelector && el.querySelector('img, svg, object, picture, svg image');
    });
  }
  function scrollToImageOnlyEdge(el, edge) {
    var maxH = Math.max(0, el.scrollWidth - el.clientWidth);
    if (edge === 'left') {
      el.scrollLeft = %1 ? -maxH : 0;
    } else {
      el.scrollLeft = %1 ? 0 : maxH;
    }
  }
  window._bibiAlignImageOnlyInitialRightEnabled = !!%3;
  window._bibiAlignImageOnlyInitialLeftEnabled = !!%4;
  window._bibiAlignImageOnlyInitialRight = function() {
    var el = document.scrollingElement || document.documentElement;
    if (window._bibiAlignImageOnlyInitialRightEnabled && isImageOnlyPage() && el.scrollWidth > el.clientWidth)
      scrollToImageOnlyEdge(el, 'right');
  };
  window._bibiAlignImageOnlyInitialLeft = function() {
    var el = document.scrollingElement || document.documentElement;
    if (window._bibiAlignImageOnlyInitialLeftEnabled && isImageOnlyPage() && el.scrollWidth > el.clientWidth)
      scrollToImageOnlyEdge(el, 'left');
  };
  Promise.all(ps).then(function(){
    requestAnimationFrame(function(){
      var el = document.documentElement;
      var imageOnly = isImageOnlyPage();
      if (%2 && !(imageOnly && (window._bibiAlignImageOnlyInitialRightEnabled || window._bibiAlignImageOnlyInitialLeftEnabled))) {
        if (el.scrollWidth > el.clientWidth)
          window.scrollBy(%1 ? -999999 : 999999, 0);
        else if (el.scrollHeight > el.clientHeight)
          window.scrollBy(0, 999999);
      } else if (%3) {
        window._bibiAlignImageOnlyInitialRight();
      } else if (%4) {
        window._bibiAlignImageOnlyInitialLeft();
      }
      if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
      requestAnimationFrame(function(){
        console.log('epub-page-ready');
      });
    });
  });
})()
