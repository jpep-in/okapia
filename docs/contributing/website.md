<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Website

The public-facing Okapia website lives in `site/`. It is an English-first static Astro site for GitHub Pages at
`https://jpep-in.github.io/okapia/`. Its source is prepared here; publication requires the workflow on `main` and
GitHub Pages configured to use **GitHub Actions**. No Apple ROM, disk image or Mac OS binary is included.

## Work locally

Use a supported even-numbered Node.js release (the workflow uses Node 24), npm, a C++14 compiler, make, Python 3
and FFmpeg. The menu renderer also needs the pinned `external/stb` submodule; no other emulator
submodule, ROM or disk image is required. On macOS, FFmpeg is available through `brew install ffmpeg`.

```sh
cd site
npm ci
npm run dev
```

The development server serves `/okapia/`. `npm run build` creates `site/dist/`; `npm run preview` serves that
production output. `npm test` checks release manifest handling and `npm run check-links` checks the built pages’
internal links, anchors and assets. Astro telemetry is disabled by the checkout-local CLI wrapper.

Both `dev` and `build` regenerate the boot-menu preview before starting Astro. To regenerate it separately, run
`npm run menu-preview`. A running dev server needs that command again after firmware changes.

## Content and interaction

- `site/src/pages/index.astro`: the landing page and the two first-run paths.
- `site/src/components/BootIllustration.astro`, `styles/boot.css`, `scripts/boot.js`: the illustrated startup,
  derived from the approved illustration. It is an animation, not an emulator or a measured boot recording.
- `tests/host/render_website.cpp` and `site/scripts/menu-preview.mjs`: current firmware renders six PNG pages
  (System, Settings, Network, Display, Information, Logs) at 1280×960, with fictional workspace/network data.
  The renderer exports each tab's actual hit rectangle to `tabs.json`, consumed by `BootMenuPreview.astro`.
  FFmpeg converts the PPM frames to PNG; no ROM, guest, private workspace or real credentials are accessed.
- `BootMenuPreview.astro` appears on Discover and in Help. Its native buttons highlight the tabs on hover/focus
  and select the corresponding PNG. Pages advance every four seconds while visible. A selection suspends this
  for twelve seconds of inactivity; pointer movement or keyboard activity renews that delay. Keyboard focus
  holds the chosen page. Reduced motion disables automatic advancement. Images and hit zones regenerate before
  each site build; no GIF or video is used.
- `site/src/scripts/site.js`: the selected host’s copy, actions and guide navigation. The visitor explicitly
  chooses Mac or Pi; the selection is stored locally when browser storage is available. The Mac is the default.
- `site/src/layouts/Layout.astro`: the single sticky header and the separate floating Mac/Pi picker on pages
  where the host changes the content. The animation reflects that choice; it has no second platform selector.
- `docs/using/`: the public guides, imported as Markdown at build time. Keep product facts in these documents and
  the relevant topic page. `astro.config.mjs` maps their relative links to the public routes or technical docs.
- `site/src/layouts/Layout.astro`: navigation, metadata, footer and English language declaration. A later French
  version can add `/fr/` routes without changing the English URLs.

Only the selected machine is visible. System 7 fills the laptop display. Each deliberate change of machine starts
one short animation, which holds at the Finder. There are no separate playback buttons or visible captions:
clicking the illustration pauses it, a second click resumes it, and a click after completion restarts it.
The illustration is a transparent native button with a changing accessible action label; Enter/Space work too. With reduced motion,
the initial view and target changes show the Finder directly. The scene uses drawn UI, not Apple software assets.

## Navigation

The primary menu is always **Discover · Get started · Help · Advanced · About**. The Mac/Pi selector floats separately
at the top of the page content, below the header, on Discover and Get started. It is absent from shared
Help, Advanced, Architecture and Downloads pages. On narrower screens it has a reserved sticky strip so it does not
cover the text. The active host is green; the other remains a neutral, clickable choice. There is no left menu.

- Discover stays on the homepage and updates the copy, illustration and first steps.
- Get started switches between `/start/mac/` and `/start/pi/`, including an initial downloads action.
  The Pi guide offers two adjacent method tabs: **Open Okapia on your Mac** (selected by default) and
  **Manual setup** (Pi ZIP, FAT32 card, root files and a minimal preferences file). `setup-methods.js` enhances the
  documentation's native disclosures into labelled tab panels with arrow/Home/End keyboard support. Without
  JavaScript they remain usable disclosures. Connection, Wi-Fi and shutdown instructions follow once, outside
  the panels.
- Help is one shared FAQ at `/help/`, including the boot menu and logs. Returning to the welcome page and Retina
  settings belong in Get started Mac; card checks belong in Get started Pi.
  The real menu preview also follows its boot-menu explanation. The Markdown marker `<!-- boot-menu-preview -->`
  places the shared Astro component between the compiled FAQ sections; keep exactly one marker.
- Advanced and Architecture are explicitly labelled for Mac and Pi, with no host picker. Their Get started
  links retain the visitor's remembered choice.
- Downloads is part of getting started and is also linked from the footer. GitHub/source links live in the footer
  and Advanced rather than competing with the main user navigation.

An explicit Mac/Pi guide URL takes precedence over the remembered host, so a deep link cannot show the wrong
selected pictogram. `site/src/lib/navigation.mjs` owns route selection; its tests cover section preservation,
shared routes and the GitHub Pages base path. The old `/help/mac/` and `/help/pi/` routes redirect to common Help.

Guide bodies are single-column, with a descriptive page title and no breadcrumbs anywhere. Links to other Advanced subjects
are part of the Advanced content, not a mixed sidebar of guides and platforms.

Advanced has a consistent tab strip above its page heading: **Overview · Architecture · Benchmarks · Making Of**. Overview is
the existing introduction and setup reference, Architecture contains the stacked illustration, and Benchmarks
presents the dated development CPU/FPU records, with explicit configurations and limitations. These are page links with
`aria-current`, so normal keyboard navigation, direct URLs and browser history work. The tab strip stays at the
same position across these shared pages; none has a Mac/Pi picker. Keep historical development runs distinct from packaged-release performance, and cite the technical benchmark record for every table.

Making Of documents the work, AI assistance and upstream credits/licences in `docs/using/making.md`.
About (`/about/`, linked at the right end of the primary menu and from the footer) is Jonathan Pepin's first-person account, sourced from
`docs/using/about.md`. New speed claims await reproducible benchmark details; the author has asked to withhold
the preliminary Pi/Mac multipliers. These shared pages have no host picker.

Both startup guides carry one `<!-- system-extensions -->` marker. The shared `docs/using/extensions.md` is
inserted there, and in Help where its direct anchor remains available. This keeps the short Sound / Shared Folder lists and installation advice in one place. System 7.0’s QuickTime
requirement stays in a brief note linking to the detailed sound guide. Keep the marker when editing the guides.

## Downloads

`/changelog/` is linked from the footer and Downloads. `npm run release` fetches the paginated list of published
GitHub releases as well as the latest stable download manifest, caching history in `.cache/history.json`.
The changelog sorts by publication date, excludes drafts, labels pre-releases and formats each release's notes.
Raw HTML, executable URLs and remote images are excluded from rendered notes. With no published entries, the
page says so; development commits and plans are not presented as releases. Release publication triggers the
existing website workflow. Local builds use the cached history; refresh it with `npm run release`.

`npm run release` reads GitHub’s latest public release and its `okapia-manifest.json`, then writes the ignored
`site/.cache/release.json`. Only archives named by that manifest and present among the official release assets
become download links. Missing archives or malformed data fail the refresh. There is no visitor-side API call.

A 404 from `releases/latest` means there is no public release: the website says so and links to the guides and
source. A local build without a refreshed cache also uses that state. Run `npm run release` before a local build
when you need the current public downloads; internet access is needed for that command and dependency installation.
The homepage offers the Apple silicon download when the manifest provides it. Other published platforms remain
available on Downloads. Minimum host OS requirements come from the manifest.

## Publish on GitHub Pages

`.github/workflows/website.yml` builds and checks pull requests without deploying them. On relevant changes to
`main`, a published release, or a manual workflow run on `main`, it builds and deploys using the `github-pages`
environment. The workflow fetches only the pinned `external/stb` submodule, installs FFmpeg and compiles the host
menu renderer; it does not need ROMs, disk images or emulator builds. Firmware, renderer, font, icon and string
changes also trigger the workflow, so deployment regenerates the menu preview. Actions are pinned
to their verified commit IDs.

In the repository’s **Settings → Pages**, select **GitHub Actions** as the publishing source. Keep any environment
protection rules consistent with `main` and release tags. Release publishing with a workflow token may not trigger
another workflow; a release tool in that situation should explicitly dispatch the Website workflow on `main`.

The repository name is the `/okapia` base path in the Astro config and Markdown link mapping. Update those together
if a custom domain or repository rename changes the path; the link helper and Vite's public font URLs follow the
configured base. Asset preparation copies the existing logo and
licensed fonts into ignored public build inputs and creates the social SVG from the logo; `assets/` remains the
source of those files. The generated npm lockfile is retained for reproducible installs.

## Content priorities

The Mac introduction makes Retina support explicit: crisp classic pixels at integer scales, with Okapia's own
menus rasterised at device-pixel density. The Advanced guide presents the specific integration improvements over
the separate Basilisk II/SheepShaver experience and links to `/advanced/architecture/`.

`site/src/components/ArchitectureStack.astro` renders that page's responsive stack: Macintosh engines, shared
platform/firmware, host interfaces, Circle and Desktop, then their destination platforms above. Linux and Windows
are grey and labelled Coming soon. The technical [Architecture](../project/architecture.md#layers) has a matching
Mermaid overview. Keep both representations aligned; the diagram is not a literal runtime call graph.

The visitor chooses one host. Mac shows the application path; Pi leads with a standalone 68K Macintosh that boots
directly on hardware, without Linux or another host OS. The Pi headline, description, primary action and first steps
change together. The host-free boot benefit also appears in the shared introduction band.

The Pi performance claim uses the measured interpreter baseline: over five times the Quadra 650 CPU reference in
Speedometer ([Benchmarks](benchmarks.md#raspberry-pi-4-rev2)). Keep that measurement attached to the CPU test and
configuration. Preliminary translator results do not describe every public build or every application's speed.

## Status

- [x] English user-facing pages, single visible Mac/Pi illustration and locally remembered choice (2026-11-24)
- [x] Guides sourced from `docs/using/`, technical documentation linked as the secondary path (2026-11-24)
- [x] Build-time release downloads, manifest tests and internal link checks (2026-11-24)
- [x] GitHub Pages build/deploy workflow prepared; no emulator build or ROM needed (2026-11-24)
- [ ] Commit/integrate the website and enable GitHub Pages; no public site is claimed yet
- [ ] French pages, after the English launch
- [ ] Real downloadable release assets, once the first application/Pi release is published

## Pitfalls

- Do not publish a speculative download, OS minimum or signing claim: the release manifest and the release notes
  decide what actually ships. Signing/notarisation is still described as preparation in the user guide.
- The System 7 illustration has its own line heights. The fixed-height volume information row must keep its text
  above its bottom border, even when the surrounding website has a generous prose line height.
- A site deployment is separate from a firmware release. Website-only work does not need the guest durability
  tests; changing emulator storage still does.

## References

- [Astro on GitHub Pages](https://docs.astro.build/en/guides/deploy/github/)
- [GitHub Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [Releases](release.md)

## JIT documentation alignment (2026-11-26)

The `wip-jit-qdgfx` changes through `1912420` are already ancestors of website merge `627897d`.
This content update reflects those technical records without merging emulator code again. Advanced and Architecture
now distinguish the in-tree translator, the reference macemu-jit backend and the interpreted PowerPC engine.
Benchmarks replaces its placeholder with the dated Pi CPU comparison (6.53 → 35.01), the Pi FPU comparison
(3.81 → 6.26) and M4 softfloat FPU timings (188 → 106 ticks). Each has its own configuration and sample count.
The public homepage retains the interpreter’s >5× Quadra CPU baseline. The author’s preliminary ×20/×200 and GHz
extrapolations stay withheld. QuickDraw and its worker are described as set aside on `wip-quickdraw`.

The public benchmark page renders its three Markdown tables as retro-styled paired bar diagrams through
`BenchmarkChart.astro`. `benchmark-charts.mjs` reads values directly from `docs/using/benchmarks.md`, so the charts
and their expandable raw tables cannot acquire different numbers. Each plot starts at zero and uses one shared
scale for its test rows. Derived FPU averages and summed M4 times are summary callouts, not additional test bars.
Scores use longer-is-better; ticks use shorter-is-better. No animation or chart library is required.
