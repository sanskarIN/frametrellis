// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { codeText, matchesGuide } from '../site/assets/docs.mjs';

test('documentation filter handles multiple search terms and mixed case', () => {
    assert.equal(matchesGuide('Keyboard shortcuts', 'KEY short'), true);
    assert.equal(matchesGuide('Windows installation', 'linux'), false);
    assert.equal(matchesGuide('Rendering in a container', 'container render'), true);
    assert.equal(matchesGuide('Project file format', ''), true);
    assert.equal(matchesGuide('Project file format', '  '), true);
});

test('documentation filter handles accents without changing the visible title', () => {
    assert.equal(matchesGuide('Résumé of project settings', 'resume'), true);
    assert.equal(matchesGuide('Editing and contributing', 'EDIT CONTRIBUTING'), true);
    assert.equal(matchesGuide('Editing and contributing', 'editing release'), false);
});

test('copy uses literal code text including indentation, special characters and line breaks', () => {
    const source = 'echo "A & B"\n  --flag <file>\n';
    const pre = { querySelector: (tag) => tag === 'code' ? { textContent: source } : null };
    assert.equal(codeText(pre), source);
    assert.equal(codeText({ querySelector: () => null }), '');
});

test('generated documentation exposes functional section anchors and the docs-only script', () => {
    const page = readFileSync(new URL('../_site/docs/user-guide/index.html', import.meta.url), 'utf8');
    assert.match(page, /<nav class="doc-toc" aria-label="On this page">/);
    assert.match(page, /href="#the-workspace"/);
    assert.match(page, /id="the-workspace"/);
    assert.match(page, /type="module" src="[^"]*assets\/docs\.mjs"/);
    assert.match(page, /class="doc-nav"/);

    const home = readFileSync(new URL('../_site/index.html', import.meta.url), 'utf8');
    assert.doesNotMatch(home, /assets\/docs\.mjs/);
    assert.doesNotMatch(home, /class="doc-toc"/);
});
