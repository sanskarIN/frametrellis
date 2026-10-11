#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Renders web/tools/og-card.html to web/site/assets/og-image.png (1200x630) with Playwright.
// Usage: node web/tools/make-og-image.mjs [path/to/playwright/index.mjs]
// Only needed when the card changes; the generated PNG is committed.

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const playwrightModule = process.argv[2] ?? 'playwright';
const { chromium } = await import(playwrightModule.startsWith('/')
    ? pathToFileURL(playwrightModule).href : playwrightModule);
const browser = await chromium.launch(process.env.CHROMIUM_PATH
    ? { executablePath: process.env.CHROMIUM_PATH } : {});
const page = await browser.newPage({ viewport: { width: 1200, height: 630 } });
await page.goto(pathToFileURL(path.join(here, 'og-card.html')).href, { waitUntil: 'load' });
await page.screenshot({ path: path.join(here, '..', 'site', 'assets', 'og-image.png') });
await browser.close();
console.log('Wrote web/site/assets/og-image.png');
