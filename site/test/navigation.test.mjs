// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { targetFromPath, pathForTarget } from '../src/lib/navigation.mjs';
const base = '/okapia/';
test('a deep link identifies its host independently of a stored choice',() => {
  assert.equal(targetFromPath('/okapia/start/pi/',base),'pi');
  assert.equal(targetFromPath('/okapia/start/mac/',base),'mac');
  assert.equal(targetFromPath('/okapia/advanced/',base),null);
  assert.equal(targetFromPath('/okapia/help/',base),null);
});
test('switching hosts preserves the Get started section',() => {
  assert.equal(pathForTarget('/okapia/start/mac/','pi',base),'/okapia/start/pi/');
});
test('shared pages keep their location when the selected host changes',() => {
  for (const path of ['/okapia/','/okapia/help/','/okapia/advanced/','/okapia/advanced/architecture/','/okapia/advanced/benchmarks/','/okapia/downloads/']) {
    assert.equal(pathForTarget(path,'pi',base),path);
  }
});
test('the Pages base is respected without matching a different repository prefix',() => {
  assert.equal(targetFromPath('/okapia-other/start/pi/',base),null);
  assert.equal(pathForTarget('/okapia-other/help/','pi',base),'/okapia-other/help/');
  assert.equal(pathForTarget('/start/mac/','pi','/'),'/start/pi/');
});
