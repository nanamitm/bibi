(function() {
    Array.from(document.querySelectorAll('mark[data-bibi-search-highlight="1"]'))
        .forEach(function(mark) {
            var parent = mark.parentNode;
            if (!parent) return;
            while (mark.firstChild)
                parent.insertBefore(mark.firstChild, mark);
            parent.removeChild(mark);
            parent.normalize();
        });
})();
