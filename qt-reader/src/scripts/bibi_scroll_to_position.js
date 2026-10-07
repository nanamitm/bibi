// Parameter: %1=scrollPosition (0.0–1.0)
(function(){
  var el = document.documentElement;
  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||
                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);
  var canH = el.scrollWidth  > el.clientWidth;
  var canV = el.scrollHeight > el.clientHeight;
  if ((vertical && canH) || (!canV && canH)) {
    // vertical-rl and RTL scroll towards negative scrollLeft. Probe the
    // browser instead of inferring it from styles: the viewport may take
    // writing-mode and direction from <body> rather than <html>.
    el.scrollLeft = -1;
    var negative = el.scrollLeft < 0;
    el.scrollLeft = (negative ? -1 : 1) * %1 * Math.max(0, el.scrollWidth - el.clientWidth);
  }
  else
    el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);
})()
