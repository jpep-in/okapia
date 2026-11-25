// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { resolveHistory, renderReleaseNotes } from '../src/lib/history.mjs';

const release = {tag_name:'26.11.1',name:'First release',published_at:'2026-11-25T10:00:00Z',
  html_url:'https://github.com/jpep-in/okapia/releases/tag/26.11.1',body:'A real published note.'};
test('history excludes drafts, preserves pre-releases and orders by publication',() => {
  const result = resolveHistory([
    {...release,published_at:'2026-11-24T10:00:00Z'},
    {...release,draft:true},
    {...release,published_at:null},
    {...release,prerelease:true},
  ]);
  assert.equal(result.length,2);
  assert.equal(result[0].prerelease,true);
  assert.equal(result[0].notes,release.body);
  assert.ok(Date.parse(result[0].date) > Date.parse(result[1].date));
});
test('empty history stays empty and invalid source metadata fails',() => {
  assert.deepEqual(resolveHistory([]),[]);
  assert.throws(() => resolveHistory([ {...release,html_url:'https://example.com/'} ]),/URL/);
  assert.throws(() => resolveHistory([ {...release,published_at:'invalid'} ]),/date/);
});
test('release notes format Markdown without raw HTML, executable links or remote images',async () => {
  const html = await renderReleaseNotes('**Fixed sound.**\n\n<script>alert(1)</script>\n\n[Bad](javascript:alert%281%29)\n\n![Track](https://example.com/track.png)');
  assert.ok(html.includes('<strong>Fixed sound.</strong>'));
  assert.ok(!html.includes('<script'));
  assert.ok(!html.includes('href="javascript:'));
  assert.ok(!html.includes('<img'));
});
