// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { readFileSync } from 'node:fs';
import { createMarkdownProcessor } from '@astrojs/markdown-remark';

export function resolveHistory(releases) {
  if (!Array.isArray(releases)) throw new Error('Release history must be an array');
  return releases.filter(item => !item.draft && item.published_at).map(item => {
    if (!item.html_url?.startsWith('https://github.com/jpep-in/okapia/releases/tag/')) throw new Error('Unexpected release history URL');
    if (!Number.isFinite(Date.parse(item.published_at))) throw new Error('Invalid release publication date');
    return { tag:item.tag_name, title:item.name || item.tag_name, date:item.published_at,
      page:item.html_url, prerelease:Boolean(item.prerelease), notes:item.body || '' };
  }).sort((a,b) => Date.parse(b.date) - Date.parse(a.date));
}
export function readHistory() {
  try { return JSON.parse(readFileSync('.cache/history.json','utf8')); }
  catch (error) { if (error.code === 'ENOENT') return []; throw error; }
}
function safeLinks() {
  return tree => {
    function walk(node) {
      if (node.properties?.href && !/^(https?:\/\/|#)/i.test(node.properties.href)) delete node.properties.href;
      if (node.children) node.children = node.children.map(child => child.tagName === 'img'
        ? {type:'text',value:child.properties?.alt || ''} : child);
      node.children?.forEach(walk);
    }
    walk(tree);
  };
}
const processor = createMarkdownProcessor({remarkRehype:{allowDangerousHtml:false},rehypePlugins:[safeLinks],syntaxHighlight:false});
export async function renderReleaseNotes(notes) {
  return (await (await processor).render(notes)).code;
}
