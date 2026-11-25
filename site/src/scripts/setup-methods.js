// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// The documentation's native disclosures become two adjacent method tabs.
document.querySelectorAll('.setup-methods').forEach(container => {
  const methods = [...container.querySelectorAll('.setup-method')];
  if (methods.length !== 2) return;
  const tabs = document.createElement('div');
  tabs.className = 'setup-method-tabs';
  tabs.setAttribute('role','tablist');
  tabs.setAttribute('aria-label','Card preparation method');
  container.prepend(tabs);
  const buttons = [], panels = [];
  methods.forEach((method,index) => {
    const summary = method.querySelector('summary');
    const button = document.createElement('button');
    button.type = 'button';
    button.id = `setup-tab-${index}`;
    button.setAttribute('role','tab');
    button.setAttribute('aria-controls',`setup-panel-${index}`);
    for (const child of summary.childNodes) button.append(child.cloneNode(true));
    tabs.append(button);
    const panel = document.createElement('section');
    panel.className = 'setup-panel';
    panel.id = `setup-panel-${index}`;
    panel.setAttribute('role','tabpanel');
    panel.setAttribute('aria-labelledby',button.id);
    for (const child of [...method.childNodes]) if (child !== summary) panel.append(child);
    method.replaceWith(panel);
    buttons.push(button);
    panels.push(panel);
  });
  const select = index => {
    buttons.forEach((button,i) => {
      button.setAttribute('aria-selected',String(i === index));
      button.tabIndex = i === index ? 0 : -1;
      panels[i].hidden = i !== index;
    });
  };
  buttons.forEach((button,index) => {
    button.addEventListener('click',() => select(index));
    button.addEventListener('keydown',event => {
      if (!['ArrowLeft','ArrowRight','Home','End'].includes(event.key)) return;
      event.preventDefault();
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : (index + 1) % buttons.length;
      select(next);
      buttons[next].focus();
    });
  });
  select(0);
});
