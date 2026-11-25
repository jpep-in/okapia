// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// Real firmware pixels, example data. Never boots an emulator or opens a disk.
import { spawnSync } from 'node:child_process';
import { mkdir, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const root = new URL('../../', import.meta.url);
const frames = new URL('../.cache/menu-preview/', import.meta.url);
const output = new URL('../public/assets/', import.meta.url);
await Promise.all([mkdir(frames,{recursive:true}),mkdir(output,{recursive:true})]);
function run(command,args) {
  const result = spawnSync(command,args,{cwd:fileURLToPath(root),stdio:'inherit'});
  if (result.error) throw new Error(`${command} is required for the boot-menu preview: ${result.error.message}`);
  if (result.status !== 0) throw new Error(`${command} failed (${result.status}) while generating the boot-menu preview`);
}
const ffmpeg = ['-hide_banner','-loglevel','error','-y'];
run('make',['-C','tests/host','render_website']);
run(fileURLToPath(new URL('tests/host/render_website',root)),[fileURLToPath(frames)]);
for (const page of ['startup','settings','network','display','info','journal']) {
  run('ffmpeg',[...ffmpeg,'-i',fileURLToPath(new URL(page + '.ppm',frames)),'-frames:v','1',
    fileURLToPath(new URL('boot-menu-' + page + '.png',output))]);
}
for (const name of ['boot-menu.gif','boot-menu.mp4','boot-menu.png']) await rm(new URL(name,output),{force:true});
