// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import { validateSourceBuildRelease } from '../tools/release-assets.mjs';

const repo = 'https://github.com/sanskarIN/frametrellis';

function fixture(tag = 'v1.5.3') {
    const names = [
        `FrameTrellis-${tag.slice(1)}-windows-x64-setup.exe`,
        `FrameTrellis-${tag.slice(1)}-windows-x64.zip`,
        `FrameTrellis-${tag.slice(1)}-windows-x64.spdx.json`,
        `FrameTrellis-${tag.slice(1)}-windows-x64-third-party-sources.tar`,
        'SHA256SUMS.txt',
    ];
    return {
        tag_name: tag, draft: false, prerelease: false,
        assets: names.map((name) => ({
            name, size: 123, state: 'uploaded', digest: 'sha256:' + 'a'.repeat(64),
            browser_download_url: `${repo}/releases/download/${tag}/${name}`,
        })),
    };
}

test('complete exact stable five-file inventory is accepted in any order', () => {
    for (const tag of ['v1.5.0', 'v1.5.1', 'v1.5.2', 'v1.5.3']) {
        const data = fixture(tag);
        data.assets.reverse();
        assert.doesNotThrow(() => validateSourceBuildRelease(data, repo));
    }
});

test('every intermediate upload count is rejected', () => {
    for (let count = 0; count < 5; ++count) {
        const data = fixture();
        data.assets = data.assets.slice(0, count);
        assert.throws(() => validateSourceBuildRelease(data, repo), /exactly five assets/);
    }
});

test('duplicate, foreign, mixed-version and extra assets cannot certify a stable release', () => {
    for (const mutate of [
        (a) => { a[1] = { ...a[0] }; },
        (a) => { a[0].name = 'FrameTrellis-1.5.2-windows-x64-setup.exe'; },
        (a) => { a[0].name = 'FrameTrellis-1.5.3-linux-x86_64.AppImage'; },
        (a) => { a.push({ ...a[0] }); },
    ]) {
        const data = fixture();
        mutate(data.assets);
        assert.throws(() => validateSourceBuildRelease(data, repo), /invalid five-file distribution/);
    }
});

test('incomplete or malformed asset metadata is rejected', () => {
    for (const [key, value] of [
        ['state', 'starter'], ['state', null], ['size', 0], ['size', -1],
        ['size', '123'], ['size', NaN], ['size', 1.5], ['digest', null],
        ['digest', 'sha256:bad'], ['digest', 'sha512:' + 'a'.repeat(64)],
        ['browser_download_url', 'https://example.org/file.exe'],
        ['browser_download_url', repo + '/releases/download/v1.5.2/file.exe'],
    ]) {
        const data = fixture();
        data.assets[0][key] = value;
        assert.throws(() => validateSourceBuildRelease(data, repo), /invalid five-file distribution/);
    }
});

test('draft, prerelease and unspecified publication flags do not qualify', () => {
    for (const [key, value] of [['draft', true], ['prerelease', true], ['draft', undefined]]) {
        const data = fixture();
        data[key] = value;
        assert.throws(() => validateSourceBuildRelease(data, repo), /not stable/);
    }
});

test('older and other distribution policies are not silently redefined', () => {
    assert.doesNotThrow(() => validateSourceBuildRelease({ tag_name: 'v1.4.0' }, repo));
    assert.doesNotThrow(() => validateSourceBuildRelease({ tag_name: 'v1.6.0' }, repo));
});
