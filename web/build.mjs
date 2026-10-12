#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Builds the FrameTrellis website into web/_site (or the directory given with --out).
//
//  * Landing pages are hand-written fragments in web/site/pages and wrapped in a shared layout.
//  * Documentation pages are rendered from the repository's Markdown files, so the docs on the
//    website and in the repository never diverge.
//  * Screenshots come from docs/images (real captures of the application).
//  * The browser edition is copied from web/editor/dist when it has been built.
//  * Every internal link, anchor and asset is checked; the build fails on anything broken.
//
// Usage: node web/build.mjs [--out <dir>] [--base /frametrellis/]
//                           [--release-json <file>] [--verify-downloads]
// --release-json takes the GitHub API description of the latest release (as written by
// `gh api repos/<owner>/<repo>/releases/latest`); the download page then links its assets.
// --verify-downloads requests every download link and fails the build if one is unreachable.
// The script uses only Node.js built-ins (Node 20 or newer).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { escapeHtml, renderMarkdown } from './markdown.mjs';

const webDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(webDir, '..');

const args = process.argv.slice(2);
const option = (name, fallback) => {
    const index = args.indexOf(name);
    return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
};
const outDir = path.resolve(option('--out', path.join(webDir, '_site')));
const basePath = option('--base', '/frametrellis/');
const releaseJson = option('--release-json', null);
const verifyDownloads = args.includes('--verify-downloads');

const SITE_URL = `https://sanskarin.github.io${basePath}`;
const REPO_URL = 'https://github.com/sanskarIN/frametrellis';
const REPO_BLOB = `${REPO_URL}/blob/main/`;
const REPO_TREE = `${REPO_URL}/tree/main/`;
const version = /project\(FrameTrellis\s+VERSION\s+([\d.]+)/.exec(
    fs.readFileSync(path.join(repoRoot, 'CMakeLists.txt'), 'utf8'))?.[1] ?? 'unknown';

// Documentation pages: repository Markdown file → page slug under docs/.
const DOC_GROUPS = [
    {
        title: 'Using FrameTrellis',
        items: [
            { src: 'docs/user-guide/installation.md', slug: 'installation', label: 'Installation',
              summary: 'Installing on Windows, building on Linux, and where data is kept.' },
            { src: 'docs/user-guide/README.md', slug: 'user-guide', label: 'User guide',
              summary: 'Every feature of the editor, from importing media to exporting.' },
            { src: 'docs/user-guide/formats.md', slug: 'formats', label: 'Supported formats',
              summary: 'The media FrameTrellis imports and the files it exports.' },
            { src: 'docs/user-guide/keyboard-shortcuts.md', slug: 'keyboard-shortcuts',
              label: 'Keyboard shortcuts', summary: 'All shortcuts, also listed in the app.' },
            { src: 'docs/releases/README.md', slug: 'releases', label: 'Releases',
              summary: 'How releases are published and where their notes live.' },
            { src: 'docs/releases/unreleased.md', slug: 'unreleased', label: 'Unreleased changes',
              summary: 'What has changed since the last release.' },
        ],
    },
    {
        title: 'Building and contributing',
        items: [
            { src: 'docs/development/building.md', slug: 'building', label: 'Building from source',
              summary: 'Build instructions for Windows 11, Linux and macOS.' },
            { src: 'docs/development/distribution.md', slug: 'distribution',
              label: 'Distribution channels',
              summary: 'Release assets, and the plans for WinGet, Flathub and Homebrew.' },
            { src: 'docs/testing.md', slug: 'testing', label: 'Testing',
              summary: 'Unit, integration and GUI tests, sanitizers and static analysis.' },
            { src: 'CONTRIBUTING.md', slug: 'contributing', label: 'Contributing',
              summary: 'How to report bugs, propose changes and send pull requests.' },
            { src: 'CODE_OF_CONDUCT.md', slug: 'code-of-conduct', label: 'Code of conduct',
              summary: 'The standards of behaviour in the project.' },
        ],
    },
    {
        title: 'Architecture',
        items: [
            { src: 'docs/architecture/overview.md', slug: 'architecture', label: 'Overview',
              summary: 'Components, threading and the principles behind the code.' },
            { src: 'docs/architecture/project-format.md', slug: 'project-format',
              label: 'Project file format', summary: 'The versioned .ftproj JSON format.' },
            { src: 'docs/architecture/playback.md', slug: 'playback', label: 'Playback',
              summary: 'How the preview engine renders frames and keeps audio in sync.' },
            { src: 'docs/architecture/rendering.md', slug: 'rendering', label: 'Rendering and export',
              summary: 'The export helper process, progress, cancellation and verification.' },
        ],
    },
    {
        title: 'Project',
        items: [
            { src: 'docs/roadmap.md', slug: 'roadmap', label: 'Roadmap',
              summary: 'Milestones up to version 1.0 and beyond.' },
            { src: 'docs/LICENSING.md', slug: 'licensing', label: 'Licensing',
              summary: 'Why GPL-3.0-or-later, and how it fits Qt, MLT and FFmpeg.' },
            { src: 'THIRD_PARTY_NOTICES.md', slug: 'third-party-notices',
              label: 'Third-party notices', summary: 'Components FrameTrellis uses and their licenses.' },
            { src: 'SECURITY.md', slug: 'security-policy', label: 'Security policy',
              summary: 'How to report a vulnerability.' },
            { src: 'docs/SECURITY.md', slug: 'security-design', label: 'Security design',
              summary: 'How untrusted projects and media are handled.' },
            { src: 'docs/maintainers.md', slug: 'maintainers', label: 'Maintainer links',
              summary: 'The verified list of the maintainer\'s profiles.' },
        ],
    },
];

const docsBySource = new Map();
for (const group of DOC_GROUPS) {
    for (const item of group.items) {
        item.out = `docs/${item.slug}/index.html`;
        docsBySource.set(item.src, item);
    }
}

const hasEditor = fs.existsSync(path.join(webDir, 'editor', 'dist', 'index.html'));
const errors = [];

// ---------------------------------------------------------------------------------------------
// Helpers

function write(relPath, content) {
    const target = path.join(outDir, relPath);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, content);
}

function copyDir(from, to) {
    fs.mkdirSync(to, { recursive: true });
    for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
        const source = path.join(from, entry.name);
        const target = path.join(to, entry.name);
        if (entry.isDirectory()) copyDir(source, target);
        else fs.copyFileSync(source, target);
    }
}

// Relative URL from the page at `fromRel` to the site path `toRel` (both relative to the root).
function relUrl(fromRel, toRel) {
    const fromDir = path.posix.dirname(fromRel);
    let target = toRel.endsWith('index.html') ? toRel.slice(0, -'index.html'.length) : toRel;
    const isDir = target === '' || target.endsWith('/');
    let rel = path.posix.relative(fromDir, target || '.');
    if (isDir) rel = rel === '' ? './' : `${rel}/`;
    return rel;
}

function rootPrefix(relPath) {
    const depth = relPath.split('/').length - 1;
    return depth === 0 ? '' : '../'.repeat(depth);
}

function canonical(relPath) {
    return SITE_URL + relPath.replace(/(^|\/)index\.html$/, '$1');
}

// ---------------------------------------------------------------------------------------------
// Layout

const NAV = [
    { id: 'home', label: 'Home', href: 'index.html' },
    { id: 'download', label: 'Download', href: 'download/index.html' },
    { id: 'docs', label: 'Docs', href: 'docs/index.html' },
    ...(hasEditor ? [{ id: 'editor', label: 'Browser edition', href: 'editor/index.html' }] : []),
];

const MAINTAINER_LINKS = [
    ['Website', 'https://sanskarin.github.io'],
    ['Blog', 'https://sanskarin.github.io/blog/'],
    ['GitHub', 'https://github.com/sanskarIN'],
    ['LinkedIn', 'https://linkedin.com/in/SanskarCodes'],
    ['dev.to', 'https://dev.to/SanskarCodes'],
    ['X', 'https://x.com/SanskarCodes'],
    ['Instagram', 'https://instagram.com/SanskarCode'],
    ['Threads', 'https://www.threads.com/@SanskarCode'],
    ['Bluesky', 'https://bsky.app/profile/SanskarCodes.bsky.social'],
];

function layout({ relPath, title, description, body, nav, absolute = false, wide = false }) {
    // The 404 page is served for arbitrary URLs, so it links with absolute paths.
    const root = absolute ? basePath : rootPrefix(relPath);
    const link = (target) => (absolute ? basePath + target.replace(/index\.html$/, '')
                                       : relUrl(relPath, target));
    const fullTitle = nav === 'home' ? title : `${title} · FrameTrellis`;
    const navHtml = NAV.map((n) => {
        const current = n.id === nav ? ' aria-current="page"' : '';
        return `<a href="${link(n.href)}"${current}>${n.label}</a>`;
    }).join('');
    const ext = (label, url) => `<li><a href="${url}" rel="noopener">${label}</a></li>`;
    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'">
<meta name="referrer" content="strict-origin-when-cross-origin">
<title>${escapeHtml(fullTitle)}</title>
<meta name="description" content="${escapeHtml(description)}">
<link rel="canonical" href="${canonical(relPath)}">
<link rel="icon" href="${root}favicon.svg" type="image/svg+xml">
<link rel="icon" href="${root}assets/icon-256.png" type="image/png" sizes="256x256">
<link rel="apple-touch-icon" href="${root}assets/icon-256.png">
<meta name="theme-color" content="#0f1216">
<meta property="og:type" content="website">
<meta property="og:site_name" content="FrameTrellis">
<meta property="og:title" content="${escapeHtml(fullTitle)}">
<meta property="og:description" content="${escapeHtml(description)}">
<meta property="og:url" content="${canonical(relPath)}">
<meta property="og:image" content="${SITE_URL}assets/og-image.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:image:alt" content="FrameTrellis logo next to a screenshot of the editor">
<meta name="twitter:card" content="summary_large_image">
<link rel="stylesheet" href="${root}assets/site.css">
<script src="${root}assets/site.js"></script>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="site-header">
  <div class="wrap">
    <a class="brand" href="${link('index.html')}"><img src="${root}assets/logo.svg" alt="" width="30" height="30"><span>Frame<b>Trellis</b></span></a>
    <nav class="nav" aria-label="Main">${navHtml}<a href="${REPO_URL}" rel="noopener">GitHub</a></nav>
    <button class="theme-toggle" type="button" aria-label="Switch colour theme" title="Switch colour theme" hidden></button>
  </div>
</header>
<main id="main"${wide ? ' class="wide"' : ''}>
${body}
</main>
<footer class="site-footer">
  <div class="wrap">
    <div class="cols">
      <div>
        <h2>FrameTrellis</h2>
        <ul>
          <li><a href="${link('download/index.html')}">Download and build</a></li>
          <li><a href="${link('docs/index.html')}">Documentation</a></li>
          <li><a href="${link('docs/roadmap/index.html')}">Roadmap</a></li>
          ${ext('Source code', REPO_URL)}
          ${ext('Report a bug', `${REPO_URL}/issues`)}
        </ul>
      </div>
      <div>
        <h2>Community</h2>
        <ul>
          <li><a href="${link('docs/contributing/index.html')}">Contributing</a></li>
          <li><a href="${link('docs/code-of-conduct/index.html')}">Code of conduct</a></li>
          <li><a href="${link('docs/security-policy/index.html')}">Security policy</a></li>
          ${ext('Discord', 'https://discord.com/channels/1547184919455989760/1547187031057113098')}
          ${ext('Contact', 'https://sanskarin.github.io/contact')}
        </ul>
      </div>
      <div>
        <h2>Maintainer: Sanskar</h2>
        <ul class="inline-list">
          ${MAINTAINER_LINKS.map(([label, url]) => ext(label, url)).join('\n          ')}
        </ul>
      </div>
    </div>
    <p class="fine">
      FrameTrellis is free software under the
      <a href="${link('docs/licensing/index.html')}">GNU General Public License, version 3 or later</a>,
      provided without any warranty. Built with Qt, MLT and FFmpeg —
      see the <a href="${link('docs/third-party-notices/index.html')}">third-party notices</a>.
      This website has no cookies, analytics or trackers.
    </p>
    <p class="fine">
      FrameTrellis is free. If you would like to support its development, you can
      <a href="https://buymeacoffee.com/SanskarCodes" rel="noopener">buy the maintainer a coffee</a>
      or contribute via <a href="https://razorpay.me/@SanskarCodes" rel="noopener">Razorpay</a> — entirely optional.
    </p>
  </div>
</footer>
</body>
</html>
`;
}

// ---------------------------------------------------------------------------------------------
// Pages

// ---------------------------------------------------------------------------------------------
// Downloads

function readRelease() {
    if (!releaseJson) return null;
    let data;
    try {
        data = JSON.parse(fs.readFileSync(releaseJson, 'utf8'));
    } catch (error) {
        errors.push(`${releaseJson}: ${error.message}`);
        return null;
    }
    if (!data || typeof data.tag_name !== 'string' || !Array.isArray(data.assets) || data.draft) {
        return null; // no published release yet
    }
    const find = (suffix) => data.assets.find((a) => typeof a.name === 'string' && a.name.endsWith(suffix));
    const release = {
        tag: data.tag_name,
        version: data.tag_name.replace(/^v/, ''),
        url: data.html_url,
        published: data.published_at ? new Date(data.published_at) : null,
        installer: find('-windows-x64-setup.exe'),
        zip: find('-windows-x64.zip'),
        sbom: find('.spdx.json'),
        sources: find('-third-party-sources.tar'),
        checksums: find('SHA256SUMS.txt'),
    };
    if (!release.installer && !release.zip) {
        errors.push(`release ${release.tag} has no Windows package assets`);
        return null;
    }
    return release;
}

function downloadsHtml(release) {
    if (!release) {
        return `<p class="notice"><strong>No release has been published yet.</strong> The first stable
      version, 1.0.0, is being prepared for Windows 11 (x64). It will be published as an installer and a
      portable ZIP once the packages have passed verification, and the download links will appear here.
      Until then you can build FrameTrellis from source, as described below.</p>`;
    }
    const size = (asset) => `${(asset.size / 2 ** 20).toFixed(0)} MB`;
    const digest = (asset) => (typeof asset.digest === 'string' && asset.digest.startsWith('sha256:')
        ? `<p class="hash">SHA-256: <code>${escapeHtml(asset.digest.slice(7))}</code></p>` : '');
    const date = release.published
        ? release.published.toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric', timeZone: 'UTC' })
        : '';
    const card = (asset, title, text, primary) => (asset ? `<div class="card download">
        <h3>${title}</h3><p>${text}</p>
        <a class="btn${primary ? ' primary' : ''}" href="${escapeHtml(asset.browser_download_url)}">Download ${escapeHtml(asset.name.endsWith('.exe') ? 'installer' : 'ZIP')} (${size(asset)})</a>
        ${digest(asset)}
      </div>` : '');
    const extra = [
        release.checksums && `<a href="${escapeHtml(release.checksums.browser_download_url)}">SHA256SUMS.txt</a>`,
        release.sbom && `<a href="${escapeHtml(release.sbom.browser_download_url)}">software bill of materials (SPDX)</a>`,
        release.sources && `<a href="${escapeHtml(release.sources.browser_download_url)}">source code of the bundled components</a> (${size(release.sources)})`,
    ].filter(Boolean).join(' · ');
    return `<p class="sub">Latest release: <strong>FrameTrellis ${escapeHtml(release.version)}</strong>${date ? `, published ${date}` : ''}.
      Read the <a href="${escapeHtml(release.url)}" rel="noopener">release notes</a> for what is new and the known limitations.</p>
    <div class="grid features">
      ${card(release.installer, 'Windows installer', 'For Windows 10 (version 1809 or later) and Windows 11, 64-bit. Installs for your user account; no administrator rights needed.', true)}
      ${card(release.zip, 'Portable ZIP', 'No installation: extract the folder anywhere, for example on a USB drive, and run <code>frametrellis.exe</code>.', false)}
      <div class="card download"><h3>Linux and macOS</h3><p>There are no packages for Linux or macOS yet. You can
        <a href="#source">build FrameTrellis from source</a> on Linux; macOS support is planned.</p></div>
    </div>
    <p class="muted">Also published with this release: ${extra}.</p>
    <p class="notice"><strong>The Windows packages are not code-signed.</strong> Windows SmartScreen may show
      “Windows protected your PC” the first time you run them. Check the SHA-256 checksum, then choose
      <em>More info</em> → <em>Run anyway</em>. The <a href="{{link:docs/installation/index.html}}">installation guide</a> explains each step.</p>`;
}

async function verifyReleaseLinks(release) {
    if (!release) return;
    const assets = [release.installer, release.zip, release.sbom, release.sources, release.checksums].filter(Boolean);
    for (const asset of assets) {
        try {
            const response = await fetch(asset.browser_download_url, { method: 'HEAD', redirect: 'follow' });
            if (!response.ok) errors.push(`download link ${asset.browser_download_url} answered ${response.status}`);
        } catch (error) {
            errors.push(`download link ${asset.browser_download_url} failed: ${error.message}`);
        }
    }
}

const latestRelease = readRelease();
// The website names a version only once it has been published as a release; until then the
// source version is labelled as a development version.
const versionLabel = latestRelease ? `Version ${latestRelease.version}` : `In development (${version})`;

function buildPages() {
    const pagesDir = path.join(webDir, 'site', 'pages');
    for (const file of fs.readdirSync(pagesDir).sort()) {
        if (!file.endsWith('.html')) continue;
        const source = fs.readFileSync(path.join(pagesDir, file), 'utf8');
        const match = /^<!--page\s*([\s\S]*?)-->\s*/.exec(source);
        if (!match) {
            errors.push(`web/site/pages/${file}: missing <!--page {...} --> header`);
            continue;
        }
        const meta = JSON.parse(match[1]);
        const absolute = Boolean(meta.absolute);
        const root = absolute ? basePath : rootPrefix(meta.out);
        let body = source.slice(match[0].length)
            .replaceAll('{{downloads}}', downloadsHtml(latestRelease))
            .replaceAll('{{root}}', root)
            .replaceAll('{{repo}}', REPO_URL)
            .replaceAll('{{version}}', version)
            .replaceAll('{{versionLabel}}', versionLabel);
        // {{link:path}} resolves a site path relative to this page.
        body = body.replace(/\{\{link:([^}]+)\}\}/g, (_, target) =>
            absolute ? basePath + target.replace(/index\.html$/, '') : relUrl(meta.out, target));
        if (/\{\{[^}]*\}\}/.test(body)) {
            errors.push(`web/site/pages/${file}: unknown placeholder ${/\{\{[^}]*\}\}/.exec(body)[0]}`);
        }
        write(meta.out, layout({ relPath: meta.out, title: meta.title, description: meta.description,
                                 body, nav: meta.nav, absolute, wide: meta.wide }));
    }
}

function docNav(currentSlug, relPath) {
    const groups = DOC_GROUPS.map((group) => {
        const items = group.items.map((item) => {
            const current = item.slug === currentSlug ? ' aria-current="page"' : '';
            return `<li><a href="${relUrl(relPath, item.out)}"${current}>${escapeHtml(item.label)}</a></li>`;
        }).join('');
        return `<h2>${escapeHtml(group.title)}</h2><ul>${items}</ul>`;
    }).join('\n');
    return `<nav class="doc-nav" aria-label="Documentation"><a class="doc-home" href="${relUrl(relPath, 'docs/index.html')}">All documentation</a>${groups}</nav>`;
}

function buildDocs() {
    const imagesUsed = new Set();
    for (const group of DOC_GROUPS) {
        for (const item of group.items) {
            const sourcePath = path.join(repoRoot, item.src);
            const markdown = fs.readFileSync(sourcePath, 'utf8');
            const resolveLink = (url, isImage) => {
                if (/^(https?:|mailto:)/.test(url)) return url;
                if (url.startsWith('#')) return url;
                const [target, fragment] = url.split('#');
                const repoPath = path.posix.normalize(
                    path.posix.join(path.posix.dirname(item.src), decodeURI(target)));
                const hash = fragment ? `#${fragment}` : '';
                if (repoPath.startsWith('..')) {
                    errors.push(`${item.src}: link leaves the repository: ${url}`);
                    return url;
                }
                const doc = docsBySource.get(repoPath);
                if (doc) {
                    return doc === item ? hash || './' : relUrl(item.out, doc.out) + hash;
                }
                if (isImage && repoPath.startsWith('docs/images/')) {
                    imagesUsed.add(repoPath);
                    return relUrl(item.out, `assets/images/${path.posix.basename(repoPath)}`);
                }
                const full = path.join(repoRoot, repoPath);
                if (!fs.existsSync(full)) {
                    errors.push(`${item.src}: broken link ${url}`);
                    return url;
                }
                const base = fs.statSync(full).isDirectory() ? REPO_TREE : REPO_BLOB;
                return base + repoPath + hash;
            };
            const { html, title } = renderMarkdown(markdown, resolveLink);
            const pageTitle = title ?? item.label;
            const body = `<div class="wrap doc">
${docNav(item.slug, item.out)}
<article class="prose">
${html}
<p class="edit-link">This page is generated from <a href="${REPO_BLOB}${item.src}" rel="noopener">${escapeHtml(item.src)}</a> in the repository. Found a mistake? <a href="${REPO_URL}/issues" rel="noopener">Open an issue</a>.</p>
</article>
</div>`;
            write(item.out, layout({ relPath: item.out, title: pageTitle, description: item.summary,
                                     body, nav: 'docs' }));
        }
    }
    return imagesUsed;
}

function buildDocsIndex() {
    const relPath = 'docs/index.html';
    const groups = DOC_GROUPS.map((group) => {
        const cards = group.items.map((item) => `<a class="card link-card" href="${relUrl(relPath, item.out)}">
  <h3>${escapeHtml(item.label)}</h3><p>${escapeHtml(item.summary)}</p></a>`).join('\n');
        return `<section class="doc-group" aria-labelledby="g-${group.items[0].slug}">
<h2 id="g-${group.items[0].slug}">${escapeHtml(group.title)}</h2>
<div class="grid">${cards}</div>
</section>`;
    }).join('\n');
    const body = `<div class="wrap page-head">
<h1>Documentation</h1>
<p class="sub">Guides for using, building and contributing to FrameTrellis. These pages are generated
from the Markdown files in the <a href="${REPO_URL}" rel="noopener">repository</a>, so they always match the code.</p>
</div>
<div class="wrap">${groups}</div>`;
    write(relPath, layout({ relPath, title: 'Documentation',
                            description: 'Guides for using, building and contributing to FrameTrellis.',
                            body, nav: 'docs' }));
}

// ---------------------------------------------------------------------------------------------
// Link checking

function listHtml(dir) {
    const files = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) files.push(...listHtml(full));
        else if (entry.name.endsWith('.html')) files.push(full);
    }
    return files;
}

function checkLinks() {
    const idCache = new Map();
    const idsOf = (file) => {
        if (!idCache.has(file)) {
            const html = fs.readFileSync(file, 'utf8');
            idCache.set(file, new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1])));
        }
        return idCache.get(file);
    };
    let checked = 0;
    for (const file of listHtml(outDir)) {
        const rel = path.relative(outDir, file).split(path.sep).join('/');
        if (rel.startsWith('editor/')) continue; // the editor bundle checks its own assets
        const html = fs.readFileSync(file, 'utf8');
        for (const m of html.matchAll(/\s(?:href|src)="([^"]*)"/g)) {
            const url = m[1].replaceAll('&amp;', '&');
            if (/^(https?:|mailto:|data:)/.test(url)) continue;
            ++checked;
            const [target, fragment] = url.split('#');
            let resolved;
            if (target === '') {
                resolved = file;
            } else if (target.startsWith('/')) {
                if (!target.startsWith(basePath)) {
                    errors.push(`${rel}: absolute link outside the site: ${url}`);
                    continue;
                }
                resolved = path.join(outDir, target.slice(basePath.length));
            } else {
                resolved = path.resolve(path.dirname(file), target);
            }
            if (!resolved.startsWith(outDir)) {
                errors.push(`${rel}: link leaves the site: ${url}`);
                continue;
            }
            if (fs.existsSync(resolved) && fs.statSync(resolved).isDirectory()) {
                resolved = path.join(resolved, 'index.html');
            }
            if (!fs.existsSync(resolved)) {
                errors.push(`${rel}: broken link ${url}`);
                continue;
            }
            if (fragment && resolved.endsWith('.html') && !idsOf(resolved).has(fragment)) {
                errors.push(`${rel}: missing anchor #${fragment} in ${url || 'the same page'}`);
            }
        }
    }
    return checked;
}

// ---------------------------------------------------------------------------------------------

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });
copyDir(path.join(webDir, 'site', 'assets'), path.join(outDir, 'assets'));
fs.copyFileSync(path.join(webDir, 'site', 'favicon.svg'), path.join(outDir, 'favicon.svg'));
fs.copyFileSync(path.join(repoRoot, 'docs', 'images', 'workspace.png'),
                path.join(outDir, 'assets', 'workspace.png'));
copyDir(path.join(repoRoot, 'docs', 'images'), path.join(outDir, 'assets', 'images'));
fs.writeFileSync(path.join(outDir, '.nojekyll'), '');
fs.writeFileSync(path.join(outDir, 'robots.txt'), `User-agent: *\nAllow: /\nSitemap: ${SITE_URL}sitemap.xml\n`);

buildPages();
buildDocs();
buildDocsIndex();
if (hasEditor) {
    copyDir(path.join(webDir, 'editor', 'dist'), path.join(outDir, 'editor'));
}

const pages = listHtml(outDir)
    .map((f) => path.relative(outDir, f).split(path.sep).join('/'))
    .filter((p) => p !== '404.html' && !(p.startsWith('editor/') && p !== 'editor/index.html'))
    .sort();
write('sitemap.xml', `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
${pages.map((p) => `  <url><loc>${canonical(p)}</loc></url>`).join('\n')}
</urlset>
`);

const checked = checkLinks();
if (verifyDownloads) {
    await verifyReleaseLinks(latestRelease);
}
if (errors.length > 0) {
    console.error(`Website build failed with ${errors.length} problem(s):`);
    for (const e of errors) console.error(`  - ${e}`);
    process.exit(1);
}
console.log(`Built ${pages.length} pages into ${path.relative(process.cwd(), outDir) || '.'}; ` +
            `${checked} internal links checked${hasEditor ? '; browser edition included' : ''}` +
            `${latestRelease ? `; downloads for ${latestRelease.tag}` : '; no release published yet'}.`);
