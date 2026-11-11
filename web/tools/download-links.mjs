// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Check GitHub release asset links without mistaking transient CDN errors for missing files.
// Never retry deterministic client failures (especially a missing 404 asset).
const retryableStatuses = new Set([408, 429, 500, 502, 503, 504]);

export async function checkDownloadLink(url, {
    request = fetch,
    pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
    maxAttempts = 3,
    timeoutMs = 8000,
} = {}) {
    let lastFailure = '';
    for (let attempt = 1; attempt <= maxAttempts; ++attempt) {
        let retryable = true;
        try {
            const response = await request(url, {
                method: 'HEAD',
                redirect: 'follow',
                signal: AbortSignal.timeout(timeoutMs),
            });
            if (response.ok) return null;
            lastFailure = `answered ${response.status}`;
            retryable = retryableStatuses.has(response.status);
        } catch (error) {
            lastFailure = `failed: ${error instanceof Error ? error.message : String(error)}`;
        }
        if (!retryable || attempt === maxAttempts) break;
        await pause(300 * attempt);
    }
    return lastFailure;
}
