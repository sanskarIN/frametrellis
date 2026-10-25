# Maintaining the documentation website

The FrameTrellis documentation website is generated directly from repository Markdown. The
published pages and the source guides must not diverge. This guide describes the **website**
tooling and navigation; it does not claim that any unreleased editor functionality is available
in the current stable build.

## Where to make changes

- Edit the relevant Markdown file under [docs](../README.md), or the root-level contributor
  and project guides, **not** the generated `web/_site` HTML files.
- The site generator is [`web/build.mjs`](../../web/build.mjs). It lists published guides in
  `DOC_GROUPS`; add a source/slug/label/summary entry there when publishing another guide.
- [`web/markdown.mjs`](../../web/markdown.mjs) renders headings, tables, lists, inline links,
  code snippets and task lists. It generates unique heading IDs for links.
- [`web/site/assets/docs.mjs`](../../web/site/assets/docs.mjs) progressively enhances
  documentation pages; the same Markdown must remain usable without the script.
- [`web/site/assets/site.css`](../../web/site/assets/site.css) controls site and documentation
  styling. Keep the sidebar, article and generated table of contents distinct.
- [`web/tests/docs-ui.test.mjs`](../../web/tests/docs-ui.test.mjs) covers filtering,
  code text fidelity and the generated pages.

## Local preview and validation

Node.js 20 or newer is sufficient; no Node package installation is needed.

```sh
node web/build.mjs --out web/_site --base /frametrellis/
node --test web/tests/*.test.mjs
```

Open `web/_site/index.html` to inspect static HTML. When testing links under the deployed
`/frametrellis/` prefix, use a local HTTP server rather than assuming `file://` URLs
behave the same as GitHub Pages. To verify the **currently published** release asset links
as well as local links, the website CI passes a GitHub release JSON file to the builder with
`--release-json` and `--verify-downloads`. The latter performs network requests; ordinary
local preview does not need it.

## Reader features and accessibility

Documentation pages include an expandable "On this page" list of level-2 and level-3
headings. Every entry is a regular fragment link, even when JavaScript is unavailable.
The heading itself receives a convenient additional `#` link if JavaScript is running.

The guide sidebar includes an optional name filter. Type terms in any order to match
available guide titles; the filter is case-insensitive and ignores common accents.
Use **/** to focus the filter when not already typing, or **Escape** to clear it.
Hidden entries are only hidden from the list; the underlying guides remain accessible
through ordinary links and browser history.

Code fences gain a **Copy code** button when the Clipboard API is available. Copying
preserves whitespace and special characters. If browser permissions or the security context
deny clipboard access, the button reports that copying is unavailable; the original code
can still be selected manually. The script uses the DOM `textContent` rather than
interpreting snippets as HTML. It does not use analytics, external APIs, or network requests.

Maintain visible focus outlines, sufficient text contrast, keyboard-operable controls,
accessible labels, non-JavaScript fallbacks and semantic heading order when changing the site.

## Pull request and release checks

The [Website workflow](../../.github/workflows/website.yml) builds the site, checks local
anchors and download links, and runs documentation UI tests on relevant pull requests.
Keep any feature still under review labeled as *unreleased* in its documentation and release
notes. The workflow checks a proposed site change; it does **not** create a GitHub release
or publish a new stable build.
