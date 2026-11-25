// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { readFileSync } from 'node:fs';
export function resolveDownloads(release, manifest) {
  if (!manifest?.okapia?.version || !Array.isArray(manifest.apps) || !Array.isArray(manifest.boards)) {
    throw new Error('Release manifest has no version, applications or boards');
  }
  const assets = new Map((release.assets || []).map(asset => [asset.name, asset]));
  function entry(item, kind) {
    const asset = assets.get(item.archive);
    if (!asset || !asset.browser_download_url?.startsWith('https://github.com/jpep-in/okapia/releases/download/')) {
      throw new Error('Missing official release asset: ' + item.archive);
    }
    return { kind, platform:item.platform || item.id, name:item.name || item.platform,
      url:asset.browser_download_url, archive:item.archive, min_os:item.min_os || '',
      size:item.size || asset.size || 0, sha256:item.sha256 || '' };
  }
  return { version:manifest.okapia.version, page:release.html_url,
    downloads:[...manifest.apps.map(item => entry(item, 'app')), ...manifest.boards.map(item => entry(item, 'board'))] };
}
export function readRelease() {
  try { return JSON.parse(readFileSync('.cache/release.json', 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return { version:null, downloads:[] }; throw error; }
}
