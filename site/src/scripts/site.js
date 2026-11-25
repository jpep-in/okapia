// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { targetFromPath, pathForTarget } from '../lib/navigation.mjs';
import './setup-methods.js';
const base = import.meta.env.BASE_URL.replace(/\/$/, '');
function savedTarget() {
  try { return localStorage.getItem('okapia-target') === 'pi' ? 'pi' : 'mac'; }
  catch { return 'mac'; }
}
let activeTarget = targetFromPath(location.pathname, base) || savedTarget();
const header = document.querySelector('.site-header-shell');
if (header) {
  const measureHeader = () => document.documentElement.style.setProperty('--header-height',header.getBoundingClientRect().height + 'px');
  measureHeader();
  new ResizeObserver(measureHeader).observe(header);
}
export function getTarget() { return activeTarget; }
function applyTarget(target) {
  activeTarget = target;
  document.body.dataset.target = target;
  document.querySelectorAll('[data-site-target]').forEach(button => {
    button.setAttribute('aria-pressed', String(button.dataset.siteTarget === target));
  });
  document.querySelectorAll('[data-for-target]').forEach(element => {
    element.hidden = element.dataset.forTarget !== target;
  });
  document.querySelectorAll('[data-start-link]').forEach(element => {
    element.href = base + '/start/' + (target === 'pi' ? 'pi/' : 'mac/');
  });
}
applyTarget(activeTarget);
if (targetFromPath(location.pathname,base)) {
  try { localStorage.setItem('okapia-target',activeTarget); } catch {}
}
document.querySelectorAll('[data-site-target]').forEach(button => {
  button.addEventListener('click',() => {
    const target = button.dataset.siteTarget;
    if (target === activeTarget) return;
    applyTarget(target);
    try { localStorage.setItem('okapia-target',target); } catch {}
    const destination = pathForTarget(location.pathname,target,base);
    if (destination !== location.pathname) {
      location.assign(destination + location.search);
      return;
    }
    document.dispatchEvent(new CustomEvent('okapia:target',{detail:target}));
  });
});
