// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import test from 'node:test';
import assert from 'node:assert/strict';
import { checkDownloadLink } from '../tools/download-links.mjs';

const url = 'https://github.com/example/project/releases/download/v1.0/example.zip';

test('retry transient 504; preserve HEAD and follow redirects', async () => {
    let attempts = 0;
    const delays = [];
    const result = await checkDownloadLink(url, {
        request: async (gotUrl, options) => {
            assert.equal(gotUrl, url);
            assert.equal(options.method, 'HEAD');
            assert.equal(options.redirect, 'follow');
            assert.ok(options.signal);
            return { ok: ++attempts === 2, status: attempts === 1 ? 504 : 200 };
        },
        pause: async ms => delays.push(ms),
    });
    assert.equal(result, null);
    assert.equal(attempts, 2);
    assert.deepEqual(delays, [300]);
});

test('a missing release asset fails on the first 404 without retry', async () => {
    let attempts = 0;
    const result = await checkDownloadLink(url, {
        request: async () => { ++attempts; return { ok: false, status: 404 }; },
        pause: async () => { throw Error('should not sleep'); },
    });
    assert.equal(result, 'answered 404');
    assert.equal(attempts, 1);
});

test('persistent server failures fail after the bounded number of attempts', async () => {
    let attempts = 0;
    const result = await checkDownloadLink(url, {
        request: async () => { ++attempts; return { ok: false, status: 503 }; },
        pause: async () => {},
    });
    assert.equal(result, 'answered 503');
    assert.equal(attempts, 3);
});

test('transient network errors can recover but permanent errors fail', async () => {
    let attempts = 0;
    const recovered = await checkDownloadLink(url, {
        request: async () => {
            if (++attempts === 1) throw Error('temporary connection reset');
            return { ok: true, status: 200 };
        },
        pause: async () => {},
    });
    assert.equal(recovered, null);
    assert.equal(attempts, 2);
    const failed = await checkDownloadLink(url, {
        request: async () => { throw Error('unreachable'); },
        pause: async () => {},
    });
    assert.equal(failed, 'failed: unreachable');
});
