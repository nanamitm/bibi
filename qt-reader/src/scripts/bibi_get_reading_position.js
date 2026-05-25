(function(){
  var el = document.documentElement;
  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||
                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);
  var canH = el.scrollWidth  > el.clientWidth;
  var canV = el.scrollHeight > el.clientHeight;
  if ((vertical && canH) || (!canV && canH))
    return Math.abs(el.scrollLeft) / Math.max(1, el.scrollWidth - el.clientWidth);
  return el.scrollTop / Math.max(1, el.scrollHeight - el.clientHeight);
})()
