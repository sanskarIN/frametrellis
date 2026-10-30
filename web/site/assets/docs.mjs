// SPDX-License-Identifier: GPL-3.0-or-later
// Progressive enhancements for documentation pages. Navigation, headings and code remain
// usable without JavaScript, and no content or clipboard data is sent to a server.

function normalize(value) {
    return String(value).normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toLocaleLowerCase();
}

export function matchesGuide(label, query) {
    const haystack = normalize(label);
    const words = normalize(query).trim().split(/\s+/).filter(Boolean);
    return words.every((word) => haystack.includes(word));
}

export function codeText(pre) {
    const code = pre.querySelector('code');
    return code ? code.textContent ?? '' : '';
}

export function enhanceDocumentation(doc) {
    const nav = doc.querySelector('.doc-nav');
    if (nav) {
        const firstHeading = nav.querySelector('h2');
        if (firstHeading) {
            const label = doc.createElement('label');
            label.className = 'doc-search-label';
            label.htmlFor = 'doc-guide-search';
            label.textContent = 'Filter guides';

            const input = doc.createElement('input');
            input.id = 'doc-guide-search';
            input.className = 'doc-guide-search';
            input.type = 'search';
            input.autocomplete = 'off';
            input.placeholder = 'Type a guide name…';

            const empty = doc.createElement('p');
            empty.className = 'doc-search-empty';
            empty.textContent = 'No matching guides.';
            empty.setAttribute('role', 'status');
            empty.hidden = true;
            nav.insertBefore(label, firstHeading);
            nav.insertBefore(input, firstHeading);
            nav.appendChild(empty);

            const groups = Array.from(nav.querySelectorAll('h2')).map((heading) => ({
                heading,
                list: heading.nextElementSibling,
            })).filter(({ list }) => list && list.tagName === 'UL');

            function filterGuides() {
                let visible = 0;
                for (const { heading, list } of groups) {
                    let count = 0;
                    for (const item of list.querySelectorAll('li')) {
                        const link = item.querySelector('a');
                        const found = !!link && matchesGuide(link.textContent, input.value);
                        item.hidden = !found;
                        if (found) ++count;
                    }
                    heading.hidden = count === 0;
                    list.hidden = count === 0;
                    visible += count;
                }
                empty.hidden = visible !== 0;
            }

            input.addEventListener('input', filterGuides);
            input.addEventListener('keydown', (event) => {
                if (event.key === 'Escape') {
                    input.value = '';
                    filterGuides();
                }
            });
            doc.addEventListener('keydown', (event) => {
                const focused = doc.activeElement;
                const tag = focused?.tagName;
                if (event.key !== '/' || event.altKey || event.ctrlKey || event.metaKey ||
                    tag === 'INPUT' || tag === 'TEXTAREA' || focused?.isContentEditable) {
                    return;
                }
                event.preventDefault();
                input.focus();
            });
        }
    }

    for (const pre of doc.querySelectorAll('.prose pre')) {
        if (!pre.querySelector('code')) continue;
        const wrapper = doc.createElement('div');
        wrapper.className = 'doc-code-block';
        pre.parentNode.insertBefore(wrapper, pre);
        wrapper.appendChild(pre);

        const button = doc.createElement('button');
        button.type = 'button';
        button.className = 'doc-copy-code';
        button.textContent = 'Copy code';
        button.setAttribute('aria-label', 'Copy code to clipboard');
        button.addEventListener('click', async () => {
            try {
                if (!navigator.clipboard?.writeText) throw new Error('Clipboard unavailable');
                await navigator.clipboard.writeText(codeText(pre));
                button.textContent = 'Copied';
                button.setAttribute('aria-label', 'Code copied to clipboard');
            } catch {
                button.textContent = 'Copy unavailable';
                button.setAttribute('aria-label', 'Copy unavailable; select the code manually');
            }
        });
        wrapper.appendChild(button);
    }

    for (const heading of doc.querySelectorAll('.prose h2[id], .prose h3[id]')) {
        const anchor = doc.createElement('a');
        anchor.href = '#' + encodeURIComponent(heading.id);
        anchor.className = 'doc-heading-anchor';
        anchor.textContent = '#';
        anchor.setAttribute('aria-label', 'Link to section: ' + heading.textContent);
        heading.appendChild(anchor);
    }
}

if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => enhanceDocumentation(document));
}
