// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
function route(pathname, base) {
  const prefix = base.replace(/\/$/, '');
  if (prefix && pathname !== prefix && !pathname.startsWith(prefix + '/')) return null;
  return pathname.slice(prefix.length).replace(/^\/+|\/+$/g, '');
}
export function targetFromPath(pathname, base) {
  return /^start\/(mac|pi)$/.exec(route(pathname, base) || '')?.[1] || null;
}
export function pathForTarget(pathname, target, base) {
  const current = route(pathname, base);
  const prefix = base.replace(/\/$/, '');
  if (current === null) return pathname;
  const host = target === 'pi' ? 'pi' : 'mac';
  if (/^start(?:\/|$)/.test(current)) return `${prefix}/start/${host}/`;
  return pathname;
}
