// Parameter: %1=scrollPosition (0.0–1.0)
(function(){
  var el = document.documentElement;
  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||
                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);
  var canH = el.scrollWidth  > el.clientWidth;
  var canV = el.scrollHeight > el.clientHeight;
  if ((vertical && canH) || (!canV && canH)) {
    var htmlStyle = getComputedStyle(el);
    var bodyStyle = document.body && getComputedStyle(document.body);
    var writingMode = htmlStyle.writingMode;
    if ((writingMode || '').indexOf('vertical') !== 0 && bodyStyle)
      writingMode = bodyStyle.writingMode;
    var negative = vertical ? writingMode === 'vertical-rl' :
      htmlStyle.direction === 'rtl' || (bodyStyle && bodyStyle.direction === 'rtl');
    el.scrollLeft = (negative ? -1 : 1) * %1 * Math.max(0, el.scrollWidth - el.clientWidth);
  }
  else
    el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);
})()
