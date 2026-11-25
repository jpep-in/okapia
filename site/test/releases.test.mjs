// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { resolveDownloads } from '../src/lib/releases.mjs';
const archive = 'Okapia-example-macos-arm64.zip';
const release = { html_url:'https://github.com/jpep-in/okapia/releases/tag/example', assets:[
  {name:archive,browser_download_url:'https://github.com/jpep-in/okapia/releases/download/example/' + archive,size:42},
  {name:'unlisted.zip',browser_download_url:'https://github.com/jpep-in/okapia/releases/download/example/unlisted.zip'},
] };
const manifest = {okapia:{version:'26.11.1'},apps:[{platform:'macos-arm64',archive,min_os:'14.0',sha256:'example'}],boards:[]};
test('only manifest-listed downloads appear, with requirements and hashes',() => {
  const resolved = resolveDownloads(release,manifest);
  assert.equal(resolved.downloads.length,1);
  assert.equal(resolved.downloads[0].platform,'macos-arm64');
  assert.equal(resolved.downloads[0].min_os,'14.0');
  assert.equal(resolved.downloads[0].sha256,'example');
  assert.equal(resolved.downloads[0].size,42);
});
test('a missing release file fails the build rather than creating a broken download',() => {
  assert.throws(() => resolveDownloads({...release,assets:[]},manifest),/Missing official/);
});
test('an off-site download is rejected',() => {
  assert.throws(() => resolveDownloads({...release,assets:[{name:archive,browser_download_url:'https://example.com/app.zip'}]},manifest),/Missing official/);
});
test('a malformed manifest fails visibly',() => {
  assert.throws(() => resolveDownloads(release,{}),/manifest/);
});
test('a future platform is described by the manifest without guessing archives',() => {
  const result = resolveDownloads(release,{...manifest,apps:[{platform:'linux-arm64',archive}],unknown:'ignored'});
  assert.equal(result.downloads[0].platform,'linux-arm64');
});
