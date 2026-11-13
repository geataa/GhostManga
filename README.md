# 🏴‍☠️ GhostManga — Ultra-Fast Native C++20 Desktop Manga Reader & AMR Port

<p align="center">
  <img src="resources/pirate-logo.png" alt="GhostManga Logo" width="120">
</p>

<p align="center">
  <a href="https://github.com/geataa/GhostManga/releases/tag/v1.0.1"><img src="https://img.shields.io/badge/Release-v1.0.1-success?style=for-the-badge&logo=github" alt="Release v1.0.1"></a>
  <a href="https://github.com/geataa/GhostManga/releases/download/v1.0.1/GhostManga.exe"><img src="https://img.shields.io/badge/Download-GhostManga.exe%20(v1.0.1)-0078D6?style=for-the-badge&logo=windows" alt="Download GhostManga.exe"></a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20x64-0078D6?logo=windows" alt="Platform: Windows x64">
  <img src="https://img.shields.io/badge/UI-Direct2D%20%2B%20D3D11%20Hardware%20Accelerated-informational" alt="Direct2D">
  <img src="https://img.shields.io/badge/Mirrors-236%20AMR%20Sources%20Embedded-orange" alt="236 Mirrors">
  <img src="https://img.shields.io/badge/i18n-15%20Languages-10b981" alt="15 Languages">
  <img src="https://img.shields.io/badge/Vibe%20Coding-Antigravity%20%7C%20Claude%20%7C%20Codex-8A2BE2" alt="100% Vibe Coded">
  <img src="https://img.shields.io/badge/Portable-Single%20EXE%20%7C%20Zero%20Registry-success" alt="Portable Single EXE">
</p>

**GhostManga** is the native Windows desktop counterpart to **All Mangas Reader (AMR)** — written from scratch in pure **C++20** with hardware-accelerated **Direct3D 11 / Direct2D / DirectComposition** rendering. Shipped as a **single ~4.7 MB portable executable (`GhostManga.exe`)** with zero external DLLs, it imports your entire AMR reading list in one click, organizes your library into **unlimited custom sub-lists**, tracks chapters in the background across **236 embedded manga sources**, reads online or offline (`.cbz` / `.zip` / folders), and bypasses anti-bot challenges seamlessly while blocking intrusive ad popups.

---

## 📥 Download v1.0.1 (Ready-to-Run Portable `.exe`)

No installer, no external DLLs, no runtime bloat. Download `GhostManga.exe`, place it anywhere, and run:

- 👉 **[Download `GhostManga.exe` (v1.0.1 Standalone Portable Executable)](https://github.com/geataa/GhostManga/releases/download/v1.0.1/GhostManga.exe)**
- 📦 **[View v1.0.1 Release Page & Release Notes](https://github.com/geataa/GhostManga/releases/tag/v1.0.1)**

All user data, custom sub-lists, reading progress, caches, and logs are stored cleanly inside the `portable_data/` folder next to `GhostManga.exe`.

---

## 🌟 Key Highlights & Product Features

### 1. ⚡ Pure Native C++20 Single-EXE Architecture
- **Zero Bloat**: No Electron, no Qt, no Node.js runtime. Pure Win32 + Direct3D 11 + Direct2D + DirectWrite + WIC.
- **Truly Self-Contained**: Statically linked (`/MT`) with **QuickJS-ng `v0.18.0`** (C11 JS engine), **Lexbor `3.1.0`** (fast HTML5/CSS selector parser), **Miniz `3.1.2`** (archive reader/writer), **WebView2 Static Loader `1.0.4314.35`**, **236 AMR mirror scripts**, and **15 language packs** embedded directly inside `GhostManga.exe`.
- **Evergreen Dynamic User-Agent**: Queries the installed Evergreen WebView2 Chromium version at runtime so HTTP requests never suffer from hardcoded browser User-Agent aging.
- **Zero Registry Pollution**: Leaves the Windows Registry untouched (with an optional Startup shortcut in the User Startup folder when enabled).

### 2. 🔍 Global Multi-Source Manga Search (AMR Style)
- **Native Endpoint Search (No Full-Site Crawling)**: Queries each mirror's native search endpoint/API directly (`GET /search?q=...`, `POST admin-ajax.php`, or REST API) with `preventimages: true`, returning fast results without hammering servers or risking IP bans.
- **Smart Grouping & Relevance Ranking**: Groups identical series across multiple mirrors into a single card sorted by exact/prefix match and relevance score.
- **One-Click Add & Status Badges**: Sources already in your library show a **`✓`** badge; new sources show a **`+`** button to add them immediately to your reading list.
- **Up to 5 Concurrent Search Tabs**: Each search tab preserves its own query, results, selected mirror, and scroll position with instant cancellation support.

### 3. 📖 Hardware-Accelerated Manga Reader & Thumbnail Film Strip
- **Multiple Reading Modes**: Continuous vertical **Webtoon** strip (with seamless spacing), **Double-Page** spread (with standalone cover option), **Single-Page**, and **RTL / LTR** direction toggle.
- **Bottom Thumbnail Strip**: Interactive film-strip preview bar at the bottom displaying page thumbnails in sequence for instant page jumping.
- **Smart Navigation**: Automatically hides the *"Next Chapter"* button when you are reading the latest available chapter; chapter selector dropdown automatically opens scrolled to your current reading position.
- **Interactive Scrollbar & HUD**: Draggable right-hand position bar, zoom controls (`+` / `-` / `0`), background brightness dimmer (`[` / `]`), one-click **Open in Browser ↗**, and toggleable side drawer (`M` / `C`).

### 4. 📚 Rich Library Management, Drag & Drop Grouping, Custom Sub-Lists & Targeted Checks
- **Unlimited Custom Sub-Lists & Whole-Group Assignment**: Create as many custom sub-lists as you want, assign individual series **or entire multi-source groups** in one click, bulk-select from the **`+ Add Manga / Group`** picker modal, rename lists, or delete a sub-list non-destructively without removing mangas from your main library.
- **Intuitive Drag & Drop Organization**:
  - **Drag onto Group Headers / Cards**: Drag any manga row or card onto another manga or group header to merge them immediately into a unified multi-source group (with one-click **`✂` Ungroup** support).
  - **Drag onto Sub-List Pills**: Drag any single manga **or an entire multi-mirror group** directly onto a **`📁 Sub-List`** pill (or onto **`+ Sub-List`** to create & assign in one move).
- **1-Click AMR Import & Export (`Ctrl+I` or Drag & Drop)**: Supports AMR V2, legacy AMR V1, and share exports (including full `customLists` and manual group overrides). Automatically backs up `library.json` to `library.json.bak` using atomic temp-file replacement (`MoveFileExW`) so data is never lost on crash or power loss.
- **Last-Read & Group Highest-Chapter Badges**:
  - Every manga card and row clearly displays your **last-read chapter** alongside the latest available chapter.
  - Multi-source **Group Deck cards & Group Headers** automatically compute and highlight the **highest chapter number** across all grouped sources so you always know which mirror is furthest ahead.
  - Color-coded status tints: 🔵 **Blue** (caught up), 🟡 **Yellow** (unread chapters waiting), 🔴 **Red** (source update/challenge error, plus a quick **`⚠️ Errors`** filter pill).
- **Active Scan Lock & Targeted Update / Chapter Checks**:
  - **Scan-In-Progress Lock**: Check buttons automatically disable and display live `X/Y` progress while an update scan is running to prevent duplicate overlapping checks.
  - **Targeted Checking (`↻`)**: Check your entire library, **only a specific multi-source group**, **only a single manga**, or **even a single individual chapter/issue** (`↻` inside the chapter list to verify live page count on demand).
- **Series Detail Modal & Smart Chapter Auto-Scroll**: Opens automatically scrolled to your **last-read chapter** for started series or **Chapter 1** for unread series. Filter *All / Unread / Read*, search chapter numbers, **Mark Read up to Latest Chapter**, **Reset Reading Progress**, manage **Sub-Lists**, open in your system browser, or manage mirror sources.
- **Double-Confirmation Library Reset**: Full library reset asks *"Are you sure?"* twice to prevent accidental data loss.

### 5. 🛡️ Two-Carrier Single-Session (2T1O), Interactive Verification & Lazy-Load Extractor
- **Fast Primary Lane + WebView2 Tandem Fallback**: Uses native WinHTTP for speed and automatically falls back to an embedded Evergreen WebView2 session when Cloudflare Turnstile / JS challenges are detected, sharing clearance cookies (`cf_clearance`) back to the native CookieJar.
- **Interactive Manual Challenge / Verification Handling (`🛡️ Verify`)**: Whenever a source demands interactive human verification (CAPTCHA, Cloudflare Turnstile, HTTP 403/503 gate), GhostManga flags the series cleanly without freezing and offers a one-click **`🛡️ Verify`** action that opens the source in the embedded browser — complete the check and click **`✅ Verified & Re-Check`** to sync cookies and re-scan that manga automatically.
- **Scroll-Triggered / Lazy-Loaded Page Extraction**: Static script parsers (`ts_reader.run`, `data-src`, `data-lazy-src`) and dynamic WebView2 container scrolling (`Read in GhostManga`) capture every page of lazy-loaded chapters rather than stopping at the first few viewport images.
- **Smart Popup & Ad Blocker**: Distinguishes real human-verification / bot challenges (*"Are you human?"* / Cloudflare Turnstile) from ad/redirect spam — silently blocking unwanted ad popups that try to flash open and close.

### 6. 💾 Offline `.CBZ` Downloads, Local Library & Recycle Bin Safety
- **One-Click Offline Download**: Save any chapter as a standard `.cbz` archive (written atomically) or raw image folder with `.ghostmanga.json` metadata.
- **Local Folders Browser**: Automatically indexes downloaded `.cbz`/`.zip` archives and cached chapters with cover previews, sorting, and deduplication.
- **Safe Deletion**: Deleting local chapters shows the full path, asks for confirmation, and moves files safely to the **Windows Recycle Bin** (`SHFileOperationW`).

### 7. 🌐 15 Built-in Interface Languages
Instant runtime switching (`L` shortcut or **Settings → Appearance**) across **15 embedded languages**:
`English` · `German` · `French` · `Spanish` · `Italian` · `Portuguese` · `Russian` · `Polish` · `Dutch` · `Japanese` · `Korean` · `Simplified Chinese` · `Indonesian` · `Azerbaijani` · `Turkish`

---

## 📘 Comprehensive User Guide

**GhostManga** combines zero-install portability, hardware-accelerated Direct2D/D3D11 rendering, and an embedded Chromium (WebView2) fallback engine into a single standalone `.exe`. Follow the sections below to get the most out of every feature.

### 1. 🚀 Portable Setup, First Launch & AMR Backup Import
- **Zero-Install Execution**: Place `GhostManga.exe` in any folder and double-click to launch. No installer wizard, runtime framework, or external DLLs are required.
- **Portable Data Directory (`portable_data/`)**: On first launch, GhostManga creates a `portable_data/` directory right next to `GhostManga.exe`. Your reading list (`library.json` and its atomic backup `library.json.bak`), custom sub-lists, settings (`settings.ini`), session cookies, and cached/downloaded chapters live exclusively inside this folder. Copy the folder to another PC or USB drive and your entire setup travels with you.
- **Importing an All Mangas Reader (AMR) Backup**:
  - Click the **`📥 Import`** button on the top toolbar or press **`Ctrl+I`**, then select your exported AMR `.json` file.
  - Or simply **Drag & Drop** any AMR `.json` backup file directly onto the GhostManga window.

### 2. 🗂️ Pinned Library & Horizontal Tab Bar Scrolling
- **Pinned Primary Navigation**: The **`📚 Library`** tab stays permanently pinned on the far left of the top tab bar — no matter how many manga chapters or embedded browser tabs you open, one click always brings you back to your library.
- **Horizontal Tab Scrolling**: When open tabs exceed the window width, the tab bar smoothly switches into horizontal scroll mode:
  - Hover over the tab bar and spin your **Mouse Wheel** to scroll left and right across tabs.
  - Click the **`<`** and **`>`** arrow buttons on either side of the scrollable tab strip.
  - Clicking or opening any tab automatically scrolls it into view.
- **Quick Tab Closing**: Click **`×`** on a tab or **Middle-Click** anywhere on a tab to close it immediately.

### 3. 📁 Unlimited Custom Sub-Lists, Drag & Drop Grouping & Bulk Picker
Organize large libraries effortlessly by grouping mangas and multi-source stacks into unlimited custom sub-lists (e.g., *Favorites*, *Action Manhwa*, *Plan to Read*, *Weekend Binges*, *Completed*):
- **Creating a Sub-List**:
  - On the **Reading List** view, click **`+ Sub-List`** in the Sub-Lists bar right below the toolbar, type a name, and press `Enter` (or click **Create**).
  - You can also drag any manga or group directly onto **`+ Sub-List`** to create a new sub-list and assign that item in one step, or click **`+ Add category`** in the Left Drawer (`☰`).
- **Four Effortless Ways to Add Mangas & Whole Groups to Sub-Lists**:
  1. **Drag & Drop onto Sub-List Pills**: Drag any manga row/card **or an entire multi-source Group Header / Deck Card** and drop it onto any **`📁 List Name`** pill in the Sub-Lists bar. All sources inside a group are added together as a complete unit.
  2. **Bulk Manga & Group Picker (`➕ Add Manga / Group`)**: Select any sub-list pill and click **`➕ Add Manga / Group`** on the Sub-Lists bar (or in the empty-list prompt) to search your general library and toggle single mangas or whole multi-source groups (`🗂 N Sources`) with one click — or use **`Add All`** for filtered results.
  3. **Direct `📁` Folder Button on Cards & Group Headers**: Click the **`📁`** icon directly on any Group Deck card, single manga card, List View group header, or series row to check/uncheck multiple sub-lists at once.
  4. **Detail Modal Sub-Lists View**: Open any manga or group detail card and click **`📁 Sub-Lists`** to manage memberships or quick-create a new list inline.
- **Drag & Drop Manual Series Grouping (Merge & Ungroup)**:
  - Have two sources with different titles (e.g., English and localized names for the same series)? Simply **drag one manga row or card and drop it onto the target Group Header (or target manga card)** to merge them into a single multi-source group!
  - Manually grouped entries persist across restarts in `library.json` and show a quick **`✂` Ungroup** button whenever you want to separate a source back out.
- **Filtering by Sub-List & Renaming / Non-Destructive Deletion**:
  - Click any **`📁 List Name (count)`** pill in the horizontal Sub-Lists bar (scroll with the mouse wheel or `<` / `>`) to filter both Card and List views immediately. Click **`All`** to return to your full library.
  - Contextual **`✎ Rename`** and **`🗑️`** buttons appear on the right when a sub-list is active. Deleting a sub-list is **100% non-destructive** — every manga and its reading history remain safe in your main Reading List.

### 4. 📚 Card Progress Badges, Smart Auto-Scroll & Targeted Update Checks
- **Card Grid & Compact List Modes with Chapter Progress**:
  - **Every Manga Card** displays both its latest known chapter and your **last-read chapter** (`📖 Read: Ch. X` or `Unread`).
  - **Multi-Source Groups (Deck Cards & Group Headers)** automatically calculate and display the **highest chapter number** (`🔥 Highest: Ch. Y`) across all grouped mirrors alongside your highest read chapter.
  - Status borders & badges: 🟡 **Yellow** (unread chapters waiting), 🔵 **Blue** (caught up), 🔴 **Red** (source check error).
- **Active Scan Lock & Targeted Checks (`↻`)**:
  - While an update check is running, global and per-item check buttons lock automatically (`⏳ X/Y`) so overlapping scans cannot be triggered accidentally.
  - Need to check just one item? Click **`↻`** on a **Group Header / Group Card** to scan only that group's mirrors, **`↻`** on a **Single Manga** to check only that series, or **`↻`** next to **any specific Chapter row** inside the chapter list to verify that chapter's live page count on the spot.
- **Interactive Error & Anti-Bot Resolution (`🛡️ Verify`)**:
  - If any source encounters a CAPTCHA, Cloudflare challenge, or HTTP block during checking, a **`⚠️ Errors (N)`** filter pill appears in the Sub-Lists bar and affected items display a **`🛡️ Verify`** button.
  - Clicking **`🛡️ Verify`** opens that manga or chapter directly inside GhostManga's embedded WebView2 browser. Solve the verification prompt manually and click **`✅ Verified & Re-Check`** in the top bar — GhostManga flushes the fresh clearance cookies into its native `CookieJar` and re-checks the series automatically.
- **Smart Chapter Auto-Scroll & Batch Actions**:
  - **Series in progress** open scrolled directly to your **last-read chapter**; **unread series** open scrolled to **Chapter 1** so you never have to scroll down past hundreds of chapters.
  - Filter chapters by *All / Unread / Read*, search by chapter number/title, **Mark Read up to Latest Chapter**, or **Reset Reading Progress** in a single click.

### 5. 🌐 Opening Series & Chapters in Your Default System Browser (`Open in Browser ↗`)
Whenever you want to view a manga page, read comments, or inspect a chapter in your system's default web browser (Chrome, Edge, Firefox, Brave, etc.):
- **In Library Detail Modal**: Click **`🌐 Open in Browser ↗`** next to the active source badge below the manga title to open the series page externally.
- **In Reader Top Bar & Side Menu (`M`)**: Click **`🌐 Open in Browser ↗`** while reading to open the current chapter URL in your default browser.
- **In Embedded Browser Tabs**: Click **`🌐 Open in Browser ↗`** on the right side of the address bar to hand off the current web page to your system browser.

### 6. ⚡ Lazy-Loaded Pages, Embedded Browser & Anti-Bot Tandem
- **Seamless Cloudflare / WAF Clearance**: When a source requires Cloudflare Turnstile or browser JS verification, GhostManga automatically routes through its embedded Evergreen WebView2 engine and shares `cf_clearance` cookies back to the fast native HTTP client.
- **Full Extraction on Scroll-Triggered / Lazy-Loaded Sites**:
  - Many modern webtoon and manga sites only populate image URLs as you scroll (`IntersectionObserver`, `data-src`, `data-lazy-src`, `ts_reader.run`, or custom nested scroll containers).
  - Both GhostManga's static mirror extractors and the embedded browser's **`⚡ Read in GhostManga`** button dynamically traverse lazy-loaded attributes and scroll containers so the **entire chapter** loads cleanly instead of stopping after the first few pages.
- **Smart Ad & Redirect Blocker**: Legitimate human-verification dialogs are permitted when needed, while background ad popups and redirect traps are intercepted and blocked silently.

### 7. 📖 Reader Modes & Visual Controls
- **Webtoon Mode (`W`)**: Continuous vertical strip layout with zero-gap stitching for manhwa and webtoons.
- **Double-Page Spread (`B`)**: Traditional two-page side-by-side layout for manga (with standalone cover page support).
- **Reading Direction (`R`)**: Switch instantaneously between Right-to-Left (Japanese manga) and Left-to-Right (manhwa / comics).
- **Bottom Thumbnail Film Strip**: Preview upcoming pages and jump to any page with one click.
- **Zoom & Ambience**: Press `+` / `-` / `0` (or `Ctrl + Mouse Wheel`) to zoom, and `[` / `]` to dim or brighten the reader backdrop.

### 8. 💾 Offline `.CBZ` Downloads & Local Archive Reader
- **One-Click `.CBZ` Archiving**: Download chapters for offline reading as standard `.cbz` archives.
- **Open Local Files (`Ctrl+O`)**: Open any local `.cbz` or `.zip` archive or image directory directly in GhostManga.
- **Recycle Bin Protection**: Deleting downloaded or local items prompts for confirmation and moves files safely to the **Windows Recycle Bin** rather than permanently erasing them.

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `←` / `→` | Previous / Next Page (respects RTL/LTR) |
| `,` / `.` | Previous / Next Chapter |
| `W` | Toggle Webtoon (Continuous Vertical Strip) Mode |
| `B` | Toggle Double-Page Spread Mode |
| `R` | Toggle Reading Direction (RTL / LTR) |
| `M` | Open / Close Reader Side Menu |
| `C` | Open Reader Side Menu & Focus Chapter List |
| `+` / `-` / `0` | Zoom In / Zoom Out / Reset Zoom |
| `[` / `]` | Adjust Background Darkness |
| `F11` | Toggle Fullscreen |
| `Ctrl+O` | Open Local Manga Folder / `.cbz` / `.zip` Archive |
| `Ctrl+I` | Import AMR Backup JSON |
| `L` | Cycle Interface Language (15 Languages) |
| `H` | Toggle Shortcuts Help Overlay |
| `Esc` | Go Back / Close Modal / Cancel Dialog |

---

## ⚡ Technical Architecture

| Layer | Technology & Implementation |
| :--- | :--- |
| **Language & Standard** | ISO C++20 (`/std:c++20`, `/O2`, `/GL`, `/MT`, MSVC 2022) & C11 |
| **Graphics & UI Shell** | Native Win32, Direct3D 11, Direct2D, DirectComposition, DirectWrite, WIC (`GhostView` immediate-mode UI) |
| **Embedded JS Runtime** | QuickJS-ng `v0.18.0` (C11 amalgamated build with 512 KB recursion guard & custom DOM/Fetch bridge) |
| **HTML5 / CSS Parser** | Lexbor `3.1.0` Static HTML5 & CSS Selectors Engine (`lexbor_static.lib`) |
| **Network & Anti-Bot** | WinHTTP (TLS 1.2/1.3, dynamic runtime WebView2 UA) + Evergreen WebView2 SDK `1.0.4314.35` Tandem Bridge (`2T1O`) |
| **Archive Engine** | Miniz `3.1.2` ZIP/CBZ reader & atomic writer with 256 MB decompression bomb guard |
| **Persistence** | Portable JSON (`portable_data/library.json` with `customLists`, `.bak` atomic failover & `settings.ini`) |

### 🔄 Keeping Components Evergreen
GhostManga is designed so its core networking, browser bridge, and mirror layers stay current over time:
- **Evergreen Runtime Browser & Dynamic User-Agent**: Uses the OS-managed Microsoft Edge WebView2 Evergreen Runtime and queries `GetAvailableCoreWebView2BrowserVersionString` at startup so `WinHTTP` User-Agent headers automatically track modern Chromium releases.
- **Hot-Swappable Mirror Bundle (`mirrors.override.js`)**: Drop an updated `mirrors.override.js` into `portable_data/` at any time to override embedded source parsers without recompiling `GhostManga.exe`.
- **Automated Dependency Updater (`scripts/update_deps.ps1`)**: Maintainers can check or pull the latest upstream **QuickJS-ng** GitHub release and **Microsoft.Web.WebView2** NuGet SDK in one command:
  ```powershell
  # Check upstream versions without modifying files
  powershell -ExecutionPolicy Bypass -File scripts\update_deps.ps1 -CheckOnly

  # Download and update third_party/quickjs and third_party/webview2 automatically
  powershell -ExecutionPolicy Bypass -File scripts\update_deps.ps1
  ```

---

## 🛠️ Building from Source & Running Tests

### Prerequisites
- **OS**: Windows 10 (1809+) or Windows 11 (x64)
- **Compiler**: Visual Studio 2022 (Community, Professional, Enterprise, or Build Tools) with C++ Desktop Development workload

### Build with One Command
All third-party headers and static libraries (`QuickJS-ng`, `Lexbor`, `Miniz`, `WebView2LoaderStatic.lib`) are vendored in `third_party/`. Simply run:

```bat
build.bat
```

This compiles and links the standalone executable to `bin\GhostManga.exe` and copies it to `GhostManga.exe`.

To clean all build artifacts:
```bat
build.bat clean
```

### Automated Test Suites
GhostManga includes unit, regression, UI, and source health test suites under `tests/`:

```bat
tests\run_review.bat
tests\run_library_regressions.bat
tests\run_ui_review.bat
tests\run_text_input.bat
```

---

## 🤖 100% Vibe Coding

This project embraces the future of software engineering: **GhostManga v1.0.1 was built 100% through Vibe Coding** orchestrated by **[@geataa](https://github.com/geataa)**:

- 🎯 **Human Vision, Product Direction & Live QA**: System architecture design, UX conception, performance specifications, and real-world validation by **[@geataa](https://github.com/geataa)**.
- 🚀 **AI Agentic Execution (Antigravity, Claude & Codex)**: End-to-end C++20 native code synthesis, Win32 message loop design, Direct2D rendering pipelines, QuickJS-ng + Lexbor mirror engine integration, WebView2 anti-bot bridge, and multi-threaded synchronization.
- ✅ **Zero Hallucination Standard**: 100% verified with deterministic automated test suites and live execution benchmarks.

We believe in radical transparency: AI-assisted development empowers solo creators to deliver robust, industrial-grade native applications at breakneck speed.

---

## 🎮 Also From SkySoftwareStudios / geataa on Steam

> By day, I work full-time as a software engineer. Evenings and weekends are dedicated to my passion for indie game development. In the spare moments left over from crafting games, I build tailored, high-performance native tools like **GhostManga** and **[Gety](https://github.com/geataa/Gety)** through modern **vibe coding** with AI.
> 
> When I'm not writing native tools, check out the game worlds I'm bringing to life on Steam:

<div align="center">

### 🧗 [BouncyClimb](https://store.steampowered.com/app/4787630/)
*A high-octane, physics-defying multiplayer climbing and platforming adventure! Race against your friends, scale hazardous heights, and master the bounce.*

[![Play BouncyClimb on Steam](https://img.shields.io/badge/Steam-BouncyClimb-1b2838?style=for-the-badge&logo=steam&logoColor=white)](https://store.steampowered.com/app/4787630/)

---

### 🍸 [Neon Angora](https://store.steampowered.com/app/4447940/)
*A groundbreaking social simulation and nightclub roleplaying experience powered by Local LLM AI. No canned dialogue trees—speak your mind, influence dynamic NPCs, uncover secrets, and experience a living story that reacts to who you choose to be.*

[![Play Neon Angora on Steam](https://img.shields.io/badge/Steam-Neon%20Angora-1b2838?style=for-the-badge&logo=steam&logoColor=white)](https://store.steampowered.com/app/4447940/)

</div>

---

## 📄 License

GhostManga is licensed under the **[MIT License](LICENSE)**.

You are free to use, modify, distribute, and incorporate GhostManga into private and commercial projects under the terms of the MIT License.

---

<p align="center">
  Crafted with ❤️, Pure C++20, and 100% Vibe Coding by <b>SkySoftwareStudios / SkySoft (geataa)</b>.
</p>
