// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { getTarget } from './site.js';

    (() => {
      const root = document.getElementById('okapia-boot-study');
      const template = root.querySelector('.ok-screen-template');
      const screens = Array.from(root.querySelectorAll('.ok-screen'));
      screens.forEach(screen => screen.appendChild(template.content.cloneNode(true)));
      const models = screens.map(screen => ({ screen, target:screen.closest('figure').dataset.target, extensions:Array.from(screen.querySelectorAll('.ok-extension')), window:screen.querySelector('.ok-window') }));
      const figures = Array.from(root.querySelectorAll('figure'));
      let activeTarget = 'mac';
      const toggles = Array.from(root.querySelectorAll('.ok-animation-toggle'));
      const reduced = window.matchMedia('(prefers-reduced-motion: reduce)');
      let frame = 0;
      let started = 0;
      let running = false;
      let elapsed = 0;
      let observer;
      const duration = 7900;
      function updateControls() {
        const action = running ? 'Pause' : elapsed < duration ? 'Resume' : 'Replay';
        toggles.forEach(button => {
          const host = button.closest('figure').dataset.target === 'mac' ? 'your Mac' : 'Raspberry Pi 4';
          button.setAttribute('aria-label',action + ' the System 7 startup animation on ' + host);
        });
      }
      function draw(time) {
        models.forEach(model => {
          if (model.target !== activeTarget) return;
          const t = time;
          model.screen.dataset.stage = t < 1900 ? 'happy' : t < 5000 ? 'welcome' : 'finder';
          const progress = Math.min(1,Math.max(0,(t - 2150) / 2700));
          model.screen.style.setProperty('--ok-load',(progress * 100).toFixed(1) + '%');
          model.extensions.forEach((extension,i) => extension.classList.toggle('is-loaded',progress > (i + 1) / 5));
          const cursorProgress = Math.min(1,Math.max(0,(t - 5600) / 900));
          model.screen.style.setProperty('--ok-cursor-x',(54 + 27 * cursorProgress) + '%');
          model.screen.style.setProperty('--ok-cursor-y',(62 - 39 * cursorProgress) + '%');
          model.window.classList.toggle('is-open',t >= 6900);
        });
      }
      function tick(now) {
        if (!running) return;
        elapsed = Math.min(duration,now - started);
        draw(elapsed);
        if (elapsed < duration) frame = requestAnimationFrame(tick);
        else { running = false; updateControls(); }
      }
      function play() {
        cancelAnimationFrame(frame);
        elapsed = 0;
        running = true;
        started = performance.now();
        updateControls();
        draw(0);
        frame = requestAnimationFrame(tick);
      }
      toggles.forEach(button => button.addEventListener('click',() => {
        observer?.disconnect();
        if (running) {
          elapsed = Math.min(duration,performance.now() - started);
          draw(elapsed);
          running = false;
          cancelAnimationFrame(frame);
          updateControls();
        } else if (elapsed < duration) {
          running = true;
          started = performance.now() - elapsed;
          updateControls();
          frame = requestAnimationFrame(tick);
        } else play();
      }));
      function showStill() {
        cancelAnimationFrame(frame);
        elapsed = duration;
        running = false;
        draw(duration);
        updateControls();
      }
      function selectTarget(target) {
        activeTarget = target;
        figures.forEach(figure => { figure.hidden = figure.dataset.target !== target; });
      }
      selectTarget(getTarget());
      document.addEventListener('okapia:target',event => {
        const target = event.detail;
        if (target === activeTarget) return;
        observer?.disconnect();
        selectTarget(target);
        if (reduced.matches) showStill(); else play();
      });
      reduced.addEventListener('change',event => { if (event.matches) showStill(); });
      if (reduced.matches) showStill();
      else {
        observer = new IntersectionObserver(entries => {
          if (entries.some(entry => entry.isIntersecting)) {
            observer.disconnect();
            play();
          }
        },{ threshold:0.15 });
        observer.observe(root);
      }
    })();
