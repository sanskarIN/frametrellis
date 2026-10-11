// SPDX-License-Identifier: GPL-3.0-or-later
// Applies the saved colour theme before first paint and wires up the theme toggle.
// The site works fully without this script; it only adds the optional toggle.
(function () {
    'use strict';
    var root = document.documentElement;
    var key = 'frametrellis-theme';
    function stored() {
        try { return localStorage.getItem(key); } catch (e) { return null; }
    }
    function save(value) {
        try { localStorage.setItem(key, value); } catch (e) { /* storage unavailable */ }
    }
    var initial = stored();
    if (initial === 'light' || initial === 'dark') {
        root.setAttribute('data-theme', initial);
    }
    function effective() {
        var explicit = root.getAttribute('data-theme');
        if (explicit) return explicit;
        return window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches
            ? 'light' : 'dark';
    }
    document.addEventListener('DOMContentLoaded', function () {
        var button = document.querySelector('.theme-toggle');
        if (!button) return;
        function update() {
            var next = effective() === 'dark' ? 'light' : 'dark';
            button.setAttribute('aria-label', 'Switch to ' + next + ' theme');
            button.title = 'Switch to ' + next + ' theme';
            button.textContent = next === 'light' ? '☀' : '☾';
        }
        button.hidden = false;
        update();
        button.addEventListener('click', function () {
            var next = effective() === 'dark' ? 'light' : 'dark';
            root.setAttribute('data-theme', next);
            save(next);
            update();
        });
    });
})();
