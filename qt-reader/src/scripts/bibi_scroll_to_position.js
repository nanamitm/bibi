// Parameter: %1=scrollPosition (0.0–1.0)
(function(){
  var el = document.documentElement;
  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||
                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);
  var canH = el.scrollWidth  > el.clientWidth;
  var canV = el.scrollHeight > el.clientHeight;
  if ((vertical && canH) || (!canV && canH))
    el.scrollLeft = %1 * Math.max(0, el.scrollWidth  - el.clientWidth);
  else
    el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);
})()
