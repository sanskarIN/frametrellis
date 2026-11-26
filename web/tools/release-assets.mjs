// SPDX-License-Identifier: GPL-3.0-or-later
// Validate the explicitly supported five-file stable distribution before Pages deploys.
// This checks GitHub's metadata, not the actual bytes or owner approval.
const SOURCE_BUILD_TAGS = new Set(['v1.5.0', 'v1.5.1', 'v1.5.2', 'v1.5.3']);
const SUFFIXES = [
    'windows-x64-setup.exe',
    'windows-x64.zip',
    'windows-x64.spdx.json',
    'windows-x64-third-party-sources.tar',
];

export function validateSourceBuildRelease(data, repoUrl) {
    if (!SOURCE_BUILD_TAGS.has(data?.tag_name)) return;
    const tag = data.tag_name;
    const reject = (reason) => {
        throw new Error(`release ${tag}: incomplete or invalid five-file distribution (${reason})`);
    };
    if (data.draft !== false || data.prerelease !== false) reject('not stable');
    if (!Array.isArray(data.assets) || data.assets.length !== 5) reject('expected exactly five assets');
    const expected = new Set([
        ...SUFFIXES.map((suffix) => `FrameTrellis-${tag.slice(1)}-${suffix}`),
        'SHA256SUMS.txt',
    ]);
    for (const asset of data.assets) {
        if (!asset || !expected.delete(asset.name)) reject('unexpected or duplicate name');
        if (asset.state !== 'uploaded') reject(`${asset.name}: upload incomplete`);
        if (!Number.isSafeInteger(asset.size) || asset.size <= 0) reject(`${asset.name}: invalid size`);
        if (typeof asset.digest !== 'string' || !/^sha256:[0-9a-f]{64}$/i.test(asset.digest)) {
            reject(`${asset.name}: missing or invalid SHA-256 metadata`);
        }
        const url = `${repoUrl}/releases/download/${tag}/${asset.name}`;
        if (asset.browser_download_url !== url) reject(`${asset.name}: unexpected download URL`);
    }
    if (expected.size !== 0) reject('missing required assets');
}
