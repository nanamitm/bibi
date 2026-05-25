(function() {
    if (window._bibiEllipsisFixed) return;
    window._bibiEllipsisFixed = true;

    var body = document.body || document.documentElement;
    var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, null);
    var nodes = [];
    var n;
    while ((n = walker.nextNode())) {
        var t = n.textContent;
        if (t.indexOf('…') >= 0 || t.indexOf('‥') >= 0) nodes.push(n);
    }

    var re = /[‥…]/g;
    nodes.forEach(function(textNode) {
        var text = textNode.textContent;
        var frag = document.createDocumentFragment();
        var last = 0;
        re.lastIndex = 0;
        var m;
        while ((m = re.exec(text)) !== null) {
            if (m.index > last)
                frag.appendChild(document.createTextNode(text.slice(last, m.index)));
            var span = document.createElement('span');
            span.style.textOrientation = 'upright';
            span.style.fontFeatureSettings = '"vert" 1';
            span.textContent = m[0];
            frag.appendChild(span);
            last = m.index + m[0].length;
        }
        if (last < text.length)
            frag.appendChild(document.createTextNode(text.slice(last)));
        textNode.parentNode.replaceChild(frag, textNode);
    });
})();
