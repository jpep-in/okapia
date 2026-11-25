// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { readdir, readFile, stat } from 'node:fs/promises';
import { join, relative, resolve } from 'node:path';
const root = resolve('dist');
async function files(directory) {
  const entries = await readdir(directory,{withFileTypes:true});
  return (await Promise.all(entries.map(entry => entry.isDirectory() ? files(join(directory,entry.name)) : join(directory,entry.name)))).flat();
}
const htmlFiles = (await files(root)).filter(file => file.endsWith('.html'));
const errors = [];
let count = 0;
for (const file of htmlFiles) {
  const html = await readFile(file,'utf8');
  for (const match of html.matchAll(/(?:href|src)="([^"\s]+)"/g)) {
    const href = match[1].replaceAll('&amp;','&');
    if (/^(https?:|mailto:|data:)/.test(href)) continue;
    const pagePath = '/okapia/' + relative(root,file).replace(/index\.html$/,'');
    const url = new URL(href,'https://local.test' + pagePath);
    if (!url.pathname.startsWith('/okapia/')) { errors.push(file + ': outside Pages base: ' + href); continue; }
    let target = join(root,decodeURIComponent(url.pathname.slice('/okapia/'.length)));
    try {
      if ((await stat(target)).isDirectory()) target = join(target,'index.html');
      const body = await readFile(target,'utf8');
      if (url.hash && !body.includes('id="' + decodeURIComponent(url.hash.slice(1)) + '"')) errors.push(file + ': missing anchor: ' + href);
      count++;
    } catch { errors.push(file + ': missing file: ' + href); }
  }
}
if (errors.length) { console.error(errors.join('\n')); process.exitCode = 1; }
else console.log(`${htmlFiles.length} pages, ${count} internal links and assets checked`);
