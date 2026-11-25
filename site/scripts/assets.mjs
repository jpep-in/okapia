// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { mkdir, copyFile, readFile, writeFile } from 'node:fs/promises';
const destination = new URL('../public/assets/', import.meta.url);
await mkdir(destination, { recursive: true });
for (const [source, name] of [
  ['logo/okapia.svg', 'okapia.svg'],
  ['fonts/texgyreheros-regular.otf', 'heros-regular.otf'],
  ['fonts/texgyreheros-bold.otf', 'heros-bold.otf'],
  ['fonts/GUST-FONT-LICENSE.txt', 'GUST-FONT-LICENSE.txt'],
]) await copyFile(new URL('../../assets/' + source, import.meta.url), new URL(name, destination));
const logo = await readFile(new URL('okapia.svg', destination), 'utf8');
const artwork = logo.slice(logo.indexOf('<g '), logo.lastIndexOf('</svg>'));
await writeFile(new URL('social.svg', destination), `<!-- Copyright (C) 2026 Jonathan Pepin; SPDX-License-Identifier: GPL-3.0-or-later -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 630"><rect width="1200" height="630" fill="#f7faf8"/><g transform="translate(90 140) scale(1.9)">${artwork}</g><g fill="#172e23" font-family="Helvetica,Arial,sans-serif"><text x="470" y="220" font-size="76" font-weight="bold">Okapia</text><text x="470" y="320" font-size="44">Run System 7</text><text x="470" y="376" font-size="44">through Mac OS 9.</text><text x="470" y="460" font-size="27">On your Mac. On Raspberry Pi.</text></g></svg>`);
