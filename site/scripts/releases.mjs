// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { mkdir, writeFile } from 'node:fs/promises';
import { resolveDownloads } from '../src/lib/releases.mjs';
import { resolveHistory } from '../src/lib/history.mjs';
const headers = { Accept:'application/vnd.github+json', 'User-Agent':'Okapia-website' };
if (process.env.GITHUB_TOKEN) headers.Authorization = 'Bearer ' + process.env.GITHUB_TOKEN;
const all = [];
for (let page = 1; ; page++) {
  const response = await fetch(`https://api.github.com/repos/jpep-in/okapia/releases?per_page=100&page=${page}`, {headers,signal:AbortSignal.timeout(20000)});
  if (!response.ok) throw new Error('GitHub history lookup failed: ' + response.status);
  const batch = await response.json();
  if (!Array.isArray(batch)) throw new Error('Unexpected GitHub release history response');
  all.push(...batch);
  if (batch.length < 100) break;
}
const history = resolveHistory(all);
const response = await fetch('https://api.github.com/repos/jpep-in/okapia/releases/latest', { headers, signal:AbortSignal.timeout(20000) });
let data = { version:null, downloads:[] };
if (response.status !== 404) {
  if (!response.ok) throw new Error('GitHub release lookup failed: ' + response.status);
  const release = await response.json();
  const asset = release.assets.find(item => item.name === 'okapia-manifest.json');
  if (!asset) throw new Error('Latest release is missing okapia-manifest.json');
  const manifest = await fetch(asset.browser_download_url, { signal:AbortSignal.timeout(20000) });
  if (!manifest.ok) throw new Error('Manifest download failed: ' + manifest.status);
  data = resolveDownloads(release, await manifest.json());
}
await mkdir(new URL('../.cache/', import.meta.url), { recursive:true });
await writeFile(new URL('../.cache/history.json', import.meta.url), JSON.stringify(history, null, 2) + '\n');
await writeFile(new URL('../.cache/release.json', import.meta.url), JSON.stringify(data, null, 2) + '\n');
console.log(data.version ? 'Release: ' + data.version : 'No public release yet');
console.log(`Release history: ${history.length} published entries`);
