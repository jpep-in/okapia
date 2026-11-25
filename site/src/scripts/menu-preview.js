// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
document.querySelectorAll('.boot-menu-preview').forEach(preview => {
  const images = [...preview.querySelectorAll('img')];
  const buttons = [...preview.querySelectorAll('[data-menu-page]')];
  let current = 0, timer, visible = false, manual = false;
  const show = index => {
    current = index;
    images.forEach((img,i) => { img.hidden = i !== index; });
    buttons.forEach((button,i) => button.setAttribute('aria-pressed',String(i === index)));
  };
  const schedule = delay => {
    clearTimeout(timer);
    if (!visible || reducedMotion.matches) return;
    timer = setTimeout(() => {
      if (preview.querySelector('button:focus-visible')) { schedule(4000); return; }
      manual = false;
      show((current + 1) % images.length);
      schedule(4000);
    },delay);
  };
  const interact = index => { manual = true; show(index); schedule(12000); };
  buttons.forEach((button,index) => button.addEventListener('click',() => interact(index)));
  preview.addEventListener('pointermove',() => { if (manual) schedule(12000); });
  preview.addEventListener('keydown',() => { if (manual) schedule(12000); });
  preview.addEventListener('focusout',() => { if (manual) schedule(12000); });
  new IntersectionObserver(entries => {
    visible = entries.some(entry => entry.isIntersecting);
    schedule(manual ? 12000 : 4000);
  },{threshold:0.35}).observe(preview);
  reducedMotion.addEventListener('change',() => schedule(manual ? 12000 : 4000));
});
