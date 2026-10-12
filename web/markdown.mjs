// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// A small, dependency-free Markdown renderer for the project documentation. It supports the
// subset the docs use (headings, paragraphs, lists with task items, fenced code, tables,
// block quotes, rules, emphasis, code spans, links, images and autolinks). Raw HTML is never
// passed through: everything is escaped, so a document cannot inject markup or scripts.

export function escapeHtml(text) {
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// GitHub-compatible heading anchors: lower case, punctuation removed, spaces to hyphens.
export function slugify(text) {
    return text
        .toLowerCase()
        .replace(/<[^>]*>/g, '')
        .replace(/[^\p{Letter}\p{Number}\s_-]/gu, '')
        .trim()
        .replace(/\s/g, '-');
}

function plainText(inline) {
    return inline
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, '$1')
        .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
        .replace(/[`*_]/g, '');
}

/**
 * Renders inline Markdown. `resolveLink(url, isImage)` maps a link target to the URL to emit
 * (or throws for a broken link).
 */
export function renderInline(text, resolveLink) {
    const out = [];
    let i = 0;
    const pushText = (s) => out.push(escapeHtml(s));
    while (i < text.length) {
        const rest = text.slice(i);
        let m;
        if (rest[0] === '\\' && /^[\\`*_{}\[\]()#+\-.!<>|]/.test(rest.slice(1))) {
            pushText(rest[1]);
            i += 2;
        } else if ((m = /^(`+)([\s\S]*?[^`])\1(?!`)/.exec(rest))) {
            out.push(`<code>${escapeHtml(m[2].replace(/^ (.*) $/, '$1'))}</code>`);
            i += m[0].length;
        } else if ((m = /^!\[([^\]]*)\]\(([^)\s]+)(?:\s+"([^"]*)")?\)/.exec(rest))) {
            const src = resolveLink(m[2], true);
            const title = m[3] ? ` title="${escapeHtml(m[3])}"` : '';
            out.push(`<img src="${escapeHtml(src)}" alt="${escapeHtml(m[1])}"${title} loading="lazy">`);
            i += m[0].length;
        } else if ((m = /^\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)\)/.exec(rest))) {
            const href = resolveLink(m[2], false);
            const external = /^https?:/.test(href);
            const rel = external ? ' rel="noopener"' : '';
            out.push(`<a href="${escapeHtml(href)}"${rel}>${renderInline(m[1], resolveLink)}</a>`);
            i += m[0].length;
        } else if ((m = /^<(https?:\/\/[^\s>]+)>/.exec(rest))) {
            out.push(`<a href="${escapeHtml(m[1])}" rel="noopener">${escapeHtml(m[1])}</a>`);
            i += m[0].length;
        } else if ((m = /^(\*\*|__)(?=\S)([\s\S]*?\S)\1/.exec(rest))) {
            out.push(`<strong>${renderInline(m[2], resolveLink)}</strong>`);
            i += m[0].length;
        } else if ((m = /^\*(?=\S)([\s\S]*?\S)\*/.exec(rest)) ||
                   (/^_/.test(rest) && (i === 0 || /[\s(]/.test(text[i - 1])) &&
                    (m = /^_(?=\S)([\s\S]*?\S)_(?![\p{Letter}\p{Number}])/u.exec(rest)))) {
            out.push(`<em>${renderInline(m[1], resolveLink)}</em>`);
            i += m[0].length;
        } else if ((m = /^ {2,}\n/.exec(rest))) {
            out.push('<br>\n');
            i += m[0].length;
        } else {
            // Consume plain text up to the next character that may start inline markup.
            m = /^[\s\S][^\\`!\[<*_ ]*/.exec(rest);
            pushText(m[0]);
            i += m[0].length;
        }
    }
    return out.join('');
}

function splitRow(line) {
    let s = line.trim();
    if (s.startsWith('|')) s = s.slice(1);
    if (s.endsWith('|') && !s.endsWith('\\|')) s = s.slice(0, -1);
    const cells = [];
    let current = '';
    let inCode = false;
    for (let i = 0; i < s.length; ++i) {
        const c = s[i];
        if (c === '\\' && s[i + 1] === '|') {
            current += '|';
            ++i;
        } else if (c === '`') {
            inCode = !inCode;
            current += c;
        } else if (c === '|' && !inCode) {
            cells.push(current.trim());
            current = '';
        } else {
            current += c;
        }
    }
    cells.push(current.trim());
    return cells;
}

const LIST_ITEM = /^(\s*)([*+-]|\d+[.)])\s+(.*)$/;

/**
 * Renders a Markdown document. Returns { html, title, headings } where headings lists
 * { level, text, id } for building a table of contents and validating anchors.
 */
export function renderMarkdown(source, resolveLink) {
    const lines = source.replace(/\r\n?/g, '\n').split('\n');
    const out = [];
    const headings = [];
    const usedIds = new Map();
    let title = null;
    let i = 0;
    const inline = (t) => renderInline(t, resolveLink);

    const uniqueId = (base) => {
        const n = usedIds.get(base) ?? 0;
        usedIds.set(base, n + 1);
        return n === 0 ? base : `${base}-${n}`;
    };
    const isBlank = (l) => l === undefined || l.trim() === '';
    const startsBlock = (l) =>
        /^\s*(#{1,6}\s|```|>|(\*\s*){3,}$|(-\s*){3,}$|\|)/.test(l) || LIST_ITEM.test(l);

    while (i < lines.length) {
        const line = lines[i];
        let m;
        if (isBlank(line)) {
            ++i;
        } else if ((m = /^\s*(```+|~~~+)\s*([\w+-]*)\s*$/.exec(line))) {
            const fence = m[1];
            const lang = m[2];
            const body = [];
            ++i;
            while (i < lines.length && !lines[i].trim().startsWith(fence)) {
                body.push(lines[i]);
                ++i;
            }
            ++i; // closing fence
            const cls = lang ? ` class="language-${escapeHtml(lang)}"` : '';
            out.push(`<pre><code${cls}>${escapeHtml(body.join('\n'))}</code></pre>`);
        } else if ((m = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line))) {
            const level = m[1].length;
            const text = m[2];
            const id = uniqueId(slugify(plainText(text)));
            headings.push({ level, text: plainText(text), id });
            if (level === 1 && title === null) title = plainText(text);
            out.push(`<h${level} id="${id}">${inline(text)}</h${level}>`);
            ++i;
        } else if (/^\s*((\*\s*){3,}|(-\s*){3,}|(_\s*){3,})$/.test(line)) {
            out.push('<hr>');
            ++i;
        } else if (/^\s*>/.test(line)) {
            const body = [];
            while (i < lines.length && /^\s*>/.test(lines[i])) {
                body.push(lines[i].replace(/^\s*> ?/, ''));
                ++i;
            }
            const inner = renderMarkdown(body.join('\n'), resolveLink);
            out.push(`<blockquote>${inner.html}</blockquote>`);
        } else if (line.includes('|') && i + 1 < lines.length &&
                   /^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$/.test(lines[i + 1])) {
            const header = splitRow(line);
            const aligns = splitRow(lines[i + 1]).map((c) =>
                c.startsWith(':') && c.endsWith(':') ? 'center' : c.endsWith(':') ? 'right' : null);
            i += 2;
            const rows = [];
            while (i < lines.length && !isBlank(lines[i]) && lines[i].includes('|')) {
                rows.push(splitRow(lines[i]));
                ++i;
            }
            const cell = (tag, text, col) => {
                const align = aligns[col] ? ` style="text-align:${aligns[col]}"` : '';
                return `<${tag}${align}>${inline(text ?? '')}</${tag}>`;
            };
            out.push('<div class="table-scroll"><table>');
            out.push(`<thead><tr>${header.map((h, c) => cell('th', h, c)).join('')}</tr></thead>`);
            out.push('<tbody>');
            for (const r of rows) {
                out.push(`<tr>${header.map((_, c) => cell('td', r[c], c)).join('')}</tr>`);
            }
            out.push('</tbody></table></div>');
        } else if ((m = LIST_ITEM.exec(line))) {
            const ordered = /\d/.test(m[2]);
            const baseIndent = m[1].length;
            const items = [];
            while (i < lines.length) {
                const lm = LIST_ITEM.exec(lines[i]);
                if (!lm || lm[1].length !== baseIndent || /\d/.test(lm[2]) !== ordered) break;
                const content = [lm[3]];
                ++i;
                // Continuation lines: indented text, or lazy continuation of the paragraph.
                while (i < lines.length && !isBlank(lines[i]) && !LIST_ITEM.test(lines[i]) &&
                       (/^\s{2,}/.test(lines[i]) || !startsBlock(lines[i]))) {
                    content.push(lines[i].trim());
                    ++i;
                }
                items.push(content.join('\n'));
                // A blank line followed by another item of the same list continues the list.
                if (isBlank(lines[i]) && i + 1 < lines.length) {
                    const next = LIST_ITEM.exec(lines[i + 1]);
                    if (next && next[1].length === baseIndent && /\d/.test(next[2]) === ordered) {
                        ++i;
                    }
                }
            }
            const start = ordered ? parseInt(m[2], 10) : 1;
            const tag = ordered ? 'ol' : 'ul';
            const startAttr = ordered && start !== 1 ? ` start="${start}"` : '';
            const isTask = items.some((t) => /^\[[ xX]\]\s/.test(t));
            out.push(`<${tag}${startAttr}${isTask ? ' class="tasks"' : ''}>`);
            for (const item of items) {
                const task = /^\[([ xX])\]\s+([\s\S]*)$/.exec(item);
                if (task) {
                    const done = task[1] !== ' ';
                    const label = done ? 'Done' : 'Not done';
                    out.push(`<li class="task ${done ? 'done' : 'todo'}"><span class="check" role="img" aria-label="${label}">${done ? '✓' : ''}</span> ${inline(task[2])}</li>`);
                } else {
                    out.push(`<li>${inline(item)}</li>`);
                }
            }
            out.push(`</${tag}>`);
        } else {
            const para = [];
            while (i < lines.length && !isBlank(lines[i]) &&
                   (para.length === 0 || !startsBlock(lines[i]))) {
                para.push(lines[i].replace(/^\s+/, ''));
                ++i;
            }
            out.push(`<p>${inline(para.join('\n'))}</p>`);
        }
    }
    return { html: out.join('\n'), title, headings };
}
