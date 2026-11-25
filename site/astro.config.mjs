// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
import { defineConfig } from 'astro/config';
import { unified } from '@astrojs/markdown-remark';
// Keep Markdown links usable both in docs/ and on the public website.
function publicDocumentationLinks() {
  const routes = { 'mac.md':'start/mac/', 'pi.md':'start/pi/', 'help.md':'help/', 'advanced.md':'advanced/', 'architecture.md':'advanced/architecture/', 'benchmarks.md':'advanced/benchmarks/', 'making.md':'advanced/making/', 'about.md':'about/', 'extensions.md':'help/#add-the-extensions-your-system-needs' };
  return tree => {
    function walk(node) {
      const href = node.properties?.href;
      if (typeof href === 'string') {
        const [path, hash] = href.split('#');
        if (routes[path]) {
          node.properties.href = '/okapia/' + routes[path] + (hash && !routes[path].includes('#') ? '#' + hash : '');
        }
        else if (path.startsWith('../')) node.properties.href = 'https://github.com/jpep-in/okapia/blob/main/docs/' + path.slice(3) + (hash ? '#' + hash : '');
      }
      node.children?.forEach(walk);
    }
    walk(tree);
  };
}
export default defineConfig({
  site: 'https://jpep-in.github.io',
  base: '/okapia',
  trailingSlash: 'always',
  redirects: { '/help/mac/':'/okapia/help/', '/help/pi/':'/okapia/help/' },
  output: 'static',
  markdown: { processor:unified({rehypePlugins:[publicDocumentationLinks]}) },
});
