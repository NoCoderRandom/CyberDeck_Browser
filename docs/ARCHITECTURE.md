# CyberDeck Browser Architecture

## Status

Accepted for v1 planning.

## Product Direction

CyberDeck Browser is a native desktop browser with a retro 1980s terminal and cyberpunk control-deck identity. The full v1 browser application is Windows-first and uses Chromium Embedded Framework (CEF) for normal web browsing and OpenGL for the Deck Space 3D bookmark system. Linux support is brought up as a separated platform target so shared core behavior can build and be tested on Linux without mixing Linux code into the Win32 shell.

The browser engine is not built by this project. CEF renders standard web pages inside native child browser views. OpenGL renders the application identity surfaces and Deck Space, where bookmarks are represented as Nodes. Live websites rendered as 3D textures are a possible v2 feature and are explicitly out of scope for v1.

## Chosen Stack

### C++20

C++20 is the primary language because it is a strong fit for a native Windows desktop application that embeds CEF, integrates with Win32 APIs, and hosts an OpenGL renderer. It gives the project direct control over memory, threading, process boundaries, window handles, and graphics resources while still providing modern language features for safer application code.

### CMake

CMake is used as the build system because it works well with Visual Studio 2022, MSVC, CEF sample layouts, native Windows targets, and future packaging steps. It keeps project configuration reviewable and avoids locking the build to hand-maintained IDE files.

### Chromium Embedded Framework

CEF is used for Chromium browsing. It provides a maintained Chromium-based rendering engine, browser process integration, request handling hooks, downloads, permissions, and web platform support. This keeps the project focused on building CyberDeck Browser rather than creating or maintaining a web engine.

### Native Win32 Windowing

Native Win32 windowing is preferred for v1 because it gives direct control over Windows 11 desktop behavior, HWND ownership, child windows, DPI handling, message loops, menus, dialogs, and CEF browser-host integration. It also avoids adding a large UI framework before the core browser shell is stable.

### Platform Separation

Platform-specific code is split by target and source directory. The Windows application remains the `CyberDeckBrowser` target and owns the Win32 shell, Windows CEF host, WGL Deck Space view, GPU preference exports, and Windows installer flow. Linux uses the `CyberDeckBrowserLinux` target, `src/platform/linux`, and Linux user-data paths. Reusable non-UI behavior lives in `CyberDeckCore`; OS services such as app-data paths, process ids, UTF conversion, and atomic file replacement go through `CyberDeckPlatform`.

### OpenGL 4.x

OpenGL 4.x is used for Deck Space, the real-time 3D bookmark view. It is mature, well documented, available on Windows GPU drivers, and sufficient for rendering neon materials, grid spaces, hex prisms, cubes, animated Nodes, and camera-controlled bookmark layouts. OpenGL is not used to replace Chromium page rendering in v1.

### JSON Persistence

JSON is used for v1 local persistence because history, settings, and bookmark Nodes need a simple readable format that is easy to inspect, back up, migrate, and recover from. A permissively licensed library such as nlohmann/json is acceptable, with license attribution documented before release.

## Rejected Alternatives

### Electron

Electron is not used for v1 because CyberDeck Browser is intended to be a native C++ Windows application with direct CEF, Win32, and OpenGL ownership. Electron would add a JavaScript/Node application shell, a larger runtime model, and less direct control over the native child browser and 3D renderer architecture.

### Qt WebEngine

Qt WebEngine is not used for v1 because it adds a large application framework and its own Chromium integration layer. The initial target is a leaner native Win32 shell around CEF, with explicit control over licensing, process setup, window parenting, and OpenGL rendering.

## Major Subsystems

### App Lifecycle

The app lifecycle owns process startup, command-line parsing, single-instance policy if added later, CEF initialization and shutdown, main window creation, message loop integration, and orderly cleanup of browser and renderer resources.

On Linux, the current lifecycle is a separated core launcher that initializes logging, settings, history, bookmarks, and navigation normalization. It is intentionally not a Linux port of the Win32 shell yet.

### CEF Integration

The CEF subsystem initializes Chromium, creates child browser views inside the native shell, handles browser callbacks, coordinates navigation state, downloads, permissions, external protocols, and custom error pages. CEF renders normal web pages for v1.

### Browser Tab Manager

The tab manager owns browser tab creation, active tab selection, close behavior, per-tab navigation state, and future restoration hooks. Each tab maps to a CEF browser instance or a controlled browser-host object.

### Toolbar and Navigation UI

The toolbar provides the v1 browser controls: back, forward, reload, stop, URL entry, search fallback, tab actions, and Add Node. It must synchronize with the active browser tab instead of keeping independent stale state.

### Terminal Mode Injection

Terminal Mode is an opt-in visual mode that injects safe CSS into web pages where supported by CEF APIs. It should be toggleable, reversible, and scoped carefully so it does not grant unsafe page capabilities or break browser security defaults.

### Bookmark Node Storage

Bookmark storage persists Vaults and Nodes as local JSON. A Vault is a
first-class bookmark container with stable metadata; a Node represents a
bookmark and stores fields such as title, URL, optional Vault id, optional
favicon or thumbnail metadata, tags, timestamps, and Deck Space layout data.
Corrupted JSON must be handled safely with backup or recovery behavior rather
than crashing.

### OpenGL Deck Space Renderer

Deck Space renders Vaults and bookmark Nodes as real 3D objects such as hex
prisms, cubes, or tiles. The Windows Vault Atlas presents the selected Vault in
the center while also showing its slot on a larger outer orbit ring. The
renderer owns GPU resources, camera controls, keyboard zoom, picking,
animation, neon materials, and layout modes. It opens URLs through the browser
tab manager, but it does not render live websites as textures in v1.

### Packaging

Packaging will produce a Windows installer using Inno Setup or NSIS. The installer stage must include application files, required CEF runtime files, license notices, user-data path expectations, and install/uninstall behavior.

Linux packaging is not implemented yet. Future Linux packaging should stay in a Linux-specific packaging directory and must not change the Windows installer flow.

## Module Diagram

```text
+-----------------------+
| CyberDeck Browser App |
+-----------+-----------+
            |
            v
+-----------------------+       +----------------------+
| Native Win32 Shell    |<----->| App Lifecycle        |
| Main HWND + messages  |       | Init/shutdown        |
+-----------+-----------+       +----------------------+
            |
            +-----------------------------+
            |                             |
            v                             v
+-----------------------+       +----------------------+
| Toolbar / Navigation  |<----->| Browser Tab Manager  |
| URL bar + controls    |       | Active tabs/state    |
+-----------+-----------+       +----------+-----------+
            |                              |
            v                              v
+-----------------------+       +----------------------+
| Terminal Mode         |       | CEF Integration      |
| CSS injection toggle  |       | Chromium rendering   |
+-----------------------+       +----------------------+

+-----------------------+       +----------------------+
| Bookmark Node Storage |<----->| Deck Space Renderer  |
| Local JSON files      |       | OpenGL 4.x 3D Nodes  |
+-----------------------+       +----------------------+

+-----------------------+
| Packaging             |
| Installer + notices   |
+-----------------------+

+-----------------------+
| Platform Layer        |
| Windows / Linux impls |
+-----------------------+
```

## V1 Non-Goals

- Building a custom web engine.
- Browser extensions.
- Cloud sync or accounts.
- Password manager.
- Ad blocker.
- Live websites mapped onto 3D objects or OpenGL textures.
- Mobile support.
- Full Linux GUI/browser feature parity in v1.
- AI features.
- Proprietary asset bundles.
- Claiming production readiness before installer, fresh install, launch, browsing, tabs, Deck Space, bookmarks, and Terminal Mode are tested.

## Implementation Notes

- Store user data under the correct Windows application data directory.
- Keep third-party licenses documented before dependencies are introduced.
- Prefer permissive dependencies such as BSD, MIT, Apache-2.0, or zlib/libpng.
- Fail safely on missing GPU features and show a clear fallback message.
- Keep each future prompt stage small and verifiable.
