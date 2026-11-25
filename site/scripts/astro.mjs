// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
// Keep build tooling self-contained, including inside a restricted checkout.
const child = spawn(process.execPath, [fileURLToPath(new URL('../node_modules/astro/bin/astro.mjs', import.meta.url)), ...process.argv.slice(2)], {
  stdio:'inherit', env:{...process.env, ASTRO_TELEMETRY_DISABLED:'1'},
});
child.on('error',error => { console.error(error.message); process.exitCode = 1; });
child.on('exit',code => { process.exitCode = code ?? 1; });
