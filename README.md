# CyberDeck Browser

CyberDeck Browser is a native desktop browser shell written in C++20. It uses
Chromium Embedded Framework (CEF) for website rendering and Deck Space, a
retro-futuristic bookmark system where bookmarks are called Nodes.

Windows and Linux are intentionally separated. The Windows application owns the
Win32 UI, Windows CEF hosting, WGL/OpenGL Deck Space, and Inno Setup packaging.
The Linux application owns its X11/CEF host, Linux Deck Space renderer, XDG data
paths, and Linux install/run scripts under `src/platform/linux`.

The visual identity is a black terminal-style interface with neon green text,
yellow highlights, red warnings/actions, monospace controls, and optional CRT
scanline/glow effects. Windows uses OpenGL for Deck Space; Linux keeps its Deck
renderer in the separated Linux shell for now.

## Promo Videos

The repository includes compact x265 promo cuts captured from the portable CEF
build:

- [Promo with text overlays](docs/media/cyberdeck-browser-promo-withtext-x265.mp4)
- [Promo without text overlays](docs/media/cyberdeck-browser-promo-notext-x265.mp4)

## Screenshots

| Terminal Mode | Nerdy Search |
| --- | --- |
| ![Google rendered with CyberDeck Terminal Mode](docs/screenshots/google-terminal-mode.jpg) | ![Google search for Hacker News and open-source browser topics](docs/screenshots/hacker-news-search.jpg) |

| Reddit | Deck Space |
| --- | --- |
| ![Reddit rendered with green terminal styling](docs/screenshots/reddit-terminal-mode.jpg) | ![Deck Space showing 3D bookmark Nodes](docs/screenshots/deck-space-nodes.jpg) |

| Node Opened |
| --- |
| ![ChatGPT opened from a Deck Space Node](docs/screenshots/chatgpt-node-opened.jpg) |

More usage notes are in [docs/USER_GUIDE.md](docs/USER_GUIDE.md). Media
capture details are in [docs/MEDIA.md](docs/MEDIA.md). Linux installation is
covered separately in [README_LINUX.md](README_LINUX.md).

## Feature Overview

- CEF-powered browsing for normal websites.
- Native Windows browser shell with Win32 controls and OpenGL Deck Space.
- Separated Linux CEF browser shell with X11 rendering and Linux-only 3D Deck
  Space projection.
- Terminal Mode CSS injection for black/green/yellow/red cyberdeck styling.
- CRT shell effects: scanlines, glow, and flicker controls.
- Local history, settings, logs, Deck Space Vaults, and Nodes stored as JSON.
- Deck Space: 3D bookmark world with Vaults and hex/cube/panel Nodes.
- Fresh profiles start with five default Vaults and 19 starter Nodes.
- Vault workflows: enter/leave, rename, delete, rotate, and keyboard zoom.
- Node workflows: Add Node, select/open, edit, delete, layout switching.
- OpenGL diagnostics for GPU vendor, renderer, and version.
- Windows installer scaffolding through Inno Setup.

## Dependencies

### Windows Browser App

- Windows 11
- Visual Studio 2022 with MSVC C++ tools for CEF-enabled builds
- CMake 3.24 or newer
- Official Windows CEF binary distribution
- Inno Setup 6 for installer creation
- A GPU/driver with compatible OpenGL support for Deck Space

This repository can also build a placeholder non-CEF shell. That mode is useful
for local development of native UI and Deck Space plumbing, but it is not a
functional web browser release.

### Linux Core Launcher

- A Linux distribution with GCC or Clang and C++20 support
- CMake 3.24 or newer
- Standard system C++ runtime and filesystem support

### Linux CEF Browser

- Ubuntu/WSL2 Ubuntu or another Linux distribution with equivalent packages
- Official Linux CEF binary distribution
- GTK/X11/NSS/GBM/ALSA runtime libraries required by CEF
- A GPU/driver with compatible OpenGL support

The Linux CEF host is separate from the Windows UI and has its own native
toolbar, Terminal Mode controls, Deck Space view, and install scripts. Full
Linux instructions are in [README_LINUX.md](README_LINUX.md).

## CEF Setup

CEF binaries are not committed to this repository. Download an official Windows
CEF binary distribution, extract it locally, then configure with `CEF_ROOT`:

```powershell
cmake -S . -B build -DCEF_ROOT="C:\path\to\cef_binary" -DCYBERDECK_REQUIRE_CEF=ON
cmake --build build --config Debug
```

Use Visual Studio 2022 or Ninja from an MSVC developer shell. Official Windows
CEF binaries are not link-compatible with the MinGW toolchain used by the local
placeholder build.

More details are in `docs/CEF_SETUP.md`.

Security note:

- Current Windows executable packaging keeps the CEF sandbox disabled until the
  Windows bootstrap/sandbox-info launch path is wired. Do not treat this build
  as a Chromium sandbox boundary yet.
- `CYBERDECK_CEF_REMOTE_DEBUGGING_PORT` enables remote debugging and binds by
  default to `127.0.0.1`.
- `CYBERDECK_CEF_REMOTE_DEBUGGING_HOST` may set a custom debug bind host only when
  `CYBERDECK_CEF_ALLOW_NONLOCAL_REMOTE_DEBUGGING=1`.
- `-CefSha256` in `build_windows_release.ps1` or `-ExpectedSha256` in
  `download_cef.ps1` pins trusted CEF archive checksums.

Linux CEF install:

```bash
./scripts/install_linux.sh --deps
```

The Linux installer downloads official CEF from the CEF Automated Builds CDN,
builds `CyberDeckBrowserLinuxCef`, and creates a local `cyberdeck-browser`
launcher. See [README_LINUX.md](README_LINUX.md) before publishing or packaging
Linux releases.

## Build

Debug build:

```powershell
cmake -S . -B build -DCEF_ROOT="C:\path\to\cef_binary" -DCYBERDECK_REQUIRE_CEF=ON
cmake --build build --config Debug
```

Release build helper:

```powershell
.\scripts\build_release.ps1 -CefRoot "C:\path\to\cef_binary" -RequireCef
```

Full Windows release helper, including CEF download, CEF-required build,
runtime verification, installer staging, and portable zip:

```powershell
.\scripts\build_windows_release.ps1 -SkipInstaller -CefSha256 "<trusted sha256>"
```

Placeholder non-CEF build:

```powershell
cmake -S . -B build
cmake --build build
```

Linux diagnostics build:

```bash
./scripts/build_linux.sh
```

Linux CEF install/build:

```bash
./scripts/install_linux.sh --deps
```

Manual Linux build:

```bash
cmake -S . -B build-linux
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

WSL2/NVIDIA debug pass:

```bash
./scripts/debug_wsl2.sh
```

Detailed Linux install, run, uninstall, and WSL2 notes are in
[README_LINUX.md](README_LINUX.md).

## Run

Multi-config generators such as Visual Studio usually place the app under the
configuration folder:

```powershell
.\build\Debug\CyberDeckBrowser.exe
.\build\Debug\CyberDeckBrowser.exe "https://www.example.com"
```

Single-config generators such as Ninja usually place the app at:

```powershell
.\build\CyberDeckBrowser.exe
.\build\CyberDeckBrowser.exe "https://www.example.com"
```

Linux diagnostics launcher:

```bash
./build-linux/cyberdeck-browser
./build-linux/cyberdeck-browser "https://www.example.com"
```

Linux CEF browser:

```bash
./scripts/run_linux.sh "https://www.example.com"
```

Installed Linux browser:

```bash
~/.local/bin/cyberdeck-browser "https://www.example.com"
```

Inside the app:

- `ADD NODE` saves the current page as a Deck Space Node.
- `DECK` enters/exits Deck Space.
- `TERM` toggles Terminal Mode CSS injection for loaded pages.
- `SCAN`, `GLOW`, and `FLK` adjust native CRT shell effects.
- `SET` opens settings and diagnostics, including CEF state, data paths, render
  path, and log path.
- In Deck Space, `Left`/`Right` or the mouse wheel rotates Vaults/Nodes,
  `Enter` opens the selected Vault or Node, `Backspace`/right click leaves a
  Vault, `+`/`-` zooms, `Delete` removes after confirmation, and `L` cycles
  Node layout mode.

## Packaging

CyberDeck Browser uses Inno Setup for Windows installer packaging.

```powershell
.\scripts\build_windows_release.ps1
```

If Inno Setup is not on `PATH`, pass the compiler path:

```powershell
.\scripts\build_windows_release.ps1 -IsccPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
```

To stage files without compiling the installer:

```powershell
.\scripts\package_installer.ps1 -SkipCompile
```

Browser downloads and installer staging do not need source tests, prompt packs,
local captures, screenshots, AI assistant memory files, or build trees. Those
remain available to developers in the repository, while `.gitattributes` keeps
development-only folders out of GitHub-generated source archives.

Packaging details are in `docs/PACKAGING.md`.

## User Data

Runtime user data is stored under:

```text
%APPDATA%\CyberDeckBrowser
```

On Linux, the separated launcher uses:

```text
${XDG_DATA_HOME}/cyberdeck-browser
```

or, when `XDG_DATA_HOME` is not set:

```text
~/.local/share/cyberdeck-browser
```

Important files and folders:

- `settings.json` - theme, shell, and Deck Space preferences.
- `history.json` - local navigation history.
- `bookmarks.json` - Deck Space Vaults and Nodes.
- `favicons/` - local placeholder favicon assets for Nodes.
- `logs/cyberdeck.log` - diagnostics log with size-based rotation.

Invalid JSON files are recovered by renaming the corrupted file and creating a
fresh default file where recovery is implemented.

## QA And Release

Use `docs/QA_CHECKLIST.md` before tagging a release candidate. Use
`docs/RELEASE_NOTES_TEMPLATE.md` to prepare release notes.

Useful docs:

- [User Guide](docs/USER_GUIDE.md)
- [Bookmark Node Model](docs/BOOKMARK_NODE_MODEL.md)
- [CEF Setup](docs/CEF_SETUP.md)
- [Linux Install Guide](README_LINUX.md)
- [Linux Support Notes](docs/LINUX.md)
- [Windows Media Playback](docs/WINDOWS_MEDIA.md)
- [Packaging](docs/PACKAGING.md)
- [Promo Media](docs/MEDIA.md)
- [QA Checklist](docs/QA_CHECKLIST.md)
- [v0.1.0-rc3 Release Notes](docs/RELEASE_NOTES_v0.1.0-rc3.md)

## Known Limitations

- A production browser release requires a CEF-enabled MSVC build. The non-CEF
  placeholder build does not render websites.
- The Linux Deck Space view uses a Linux-only software 3D projection today; the
  Windows Deck Space view remains the WGL/OpenGL implementation.
- Real favicon capture from CEF is not implemented yet; Deck Space currently
  uses local placeholder favicon badges.
- Deck Space thumbnails are not implemented.
- Reddit and some YouTube video playback require a Windows CEF build with
  H.264/AAC codec support and the related licensing cleared.
- Installer compilation requires Inno Setup 6 on the packaging machine.
- The installer is not signed and there is no auto-update channel.
- No clean Windows VM install/uninstall pass has been completed in this
  environment.
- This project has not received a full production security audit.
