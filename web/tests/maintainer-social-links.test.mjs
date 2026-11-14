// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const read = (path) => readFileSync(new URL(path, import.meta.url), 'utf8');

const profiles = [
    ['LinkedIn', 'https://linkedin.com/in/sanskarIN'],
    ['dev.to', 'https://dev.to/sanskarIN'],
    ['X', 'https://x.com/SanskarCodes'],
    ['Facebook', 'https://facebook.com/sanskarIN'],
    ['Bluesky', 'https://sanskarin.bsky.social'],
];

test('website footer and maintainer documents share current social profiles', () => {
    const homepage = read('../_site/index.html');
    const download = read('../_site/download/index.html');
    const readme = read('../../README.md');
    const maintainer = read('../../docs/maintainers.md');
    const siteBuilder = read('../build.mjs');

    for (const [label, url] of profiles) {
        for (const [name, source] of [
            ['website homepage', homepage],
            ['website download page', download],
            ['README', readme],
            ['maintainer documentation', maintainer],
            ['website builder', siteBuilder],
        ]) {
            assert.ok(source.includes(url), `${label} profile missing in ${name}`);
        }
        assert.ok(homepage.includes(`href="${url}" rel="noopener">${label}</a>`));
    }
});

test('retired social profiles are absent from published and source links', () => {
    const oldUrls = [
        'https://linkedin.com/in/' + 'SanskarCodes',
        'https://dev.to/' + 'SanskarCodes',
        'https://instagram.com/' + 'SanskarCode',
        'https://www.threads.com/' + '@SanskarCode',
        'https://bsky.app/profile/' + 'SanskarCodes.bsky.social',
    ];
    const sources = [
        ['website homepage', read('../_site/index.html')],
        ['website download page', read('../_site/download/index.html')],
        ['README', read('../../README.md')],
        ['maintainer documentation', read('../../docs/maintainers.md')],
        ['website builder', read('../build.mjs')],
    ];
    for (const [name, contents] of sources) {
        for (const legacy of oldUrls) {
            assert.ok(!contents.includes(legacy), `Retired profile ${legacy} in ${name}`);
        }
    }
    for (const [name, contents] of sources.slice(0, 2)) {
        assert.ok(!contents.includes('>Instagram</a>'), `Instagram link in ${name}`);
        assert.ok(!contents.includes('>Threads</a>'), `Threads link in ${name}`);
    }
});
