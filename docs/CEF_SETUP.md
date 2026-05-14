# CEF Setup

CyberDeck Browser uses Chromium Embedded Framework (CEF) for normal web page
rendering in the Windows browser application. CEF binaries are large and must
not be committed directly to this repository.

## Expected Layout

Download an official CEF binary distribution and extract it under a local path.
For Windows, a typical layout is:

```text
third_party/
  cef_binary_<version>_windows64/
    include/
      cef_version.h
```

The exact CEF version must be recorded in `THIRD_PARTY_NOTICES.md` before CEF
runtime files are bundled in a release.

## Configure

Without CEF headers, the current scaffold still builds as a launch-and-exit
Windows executable:

```powershell
cmake -S . -B build
cmake --build build
```

With CEF headers available:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCEF_ROOT="C:\path\to\cef_binary"
cmake --build build
```

Or use the Windows release helper to download the default official CEF build
and produce a CEF-enabled release package:

```powershell
.\scripts\build_windows_release.ps1 -SkipInstaller -CefSha256 "<trusted sha256>"
```

Use this helper when checksum pinning is required:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_cef.ps1 `
  -CefUrl "https://cef-builds.spotifycdn.com/<cef-archive>.tar.bz2" `
  -ExpectedSha256 "<trusted sha256>"
```

`build_windows_release.ps1` checks `scripts\cef_windows_downloads.sha256` for the
default archive checksum. If no trusted checksum is found locally or from a
matching `.sha256` sidecar, it requires an explicit checksum argument.

To require CEF during configure:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCEF_ROOT="C:\path\to\cef_binary" -DCYBERDECK_REQUIRE_CEF=ON
```

The CMake integration expects a standard CEF binary distribution containing:

```text
include/cef_version.h
cmake/FindCEF.cmake
libcef_dll/CMakeLists.txt
Debug/ or Release/ runtime binaries
Resources/ runtime resource files
```

The build links the official `libcef_dll_wrapper` target and copies CEF runtime
binaries/resources next to `CyberDeckBrowser.exe` after build.

### Windows Media Codecs

CEF setup also decides what media codecs CyberDeck can play. The default CEF
binary distribution can render normal pages, but H.264/AVC and AAC support may
be absent because Chromium treats them as proprietary codecs. Reddit and some
YouTube streams rely on those formats.

For a Windows release that claims Reddit/YouTube video playback support, build
and package against a CEF distribution produced from official source with the
required proprietary codec support enabled, then clear the related licensing
requirements. Keep the exact CEF version and codec build flags in the release
notes and third-party notices. More detail is in `docs/WINDOWS_MEDIA.md`.

Official Windows CEF binaries require an MSVC-compatible build. MinGW builds
remain useful for non-CEF scaffold checks, but they intentionally leave CEF
disabled.

## Linux Status

Linux support is separated from the Windows app path. The non-CEF Linux core
launcher remains `CyberDeckBrowserLinux`, and the Linux CEF host is
`CyberDeckBrowserLinuxCef`, installed as `cyberdeck-browser-cef`.

Easy Ubuntu/WSL2 install:

```bash
./scripts/install_linux.sh --deps
```

That script downloads an official Linux CEF binary archive from the CEF
Automated Builds CDN, extracts it under `third_party/cef/linux64`, configures
with `-DCEF_ROOT=<that-path> -DCYBERDECK_REQUIRE_CEF=ON`, builds the separated
Linux CEF target, runs tests, and creates a `cyberdeck-browser` wrapper under
`~/.local/bin`.

Manual Linux configure:

```bash
cmake -S . -B build-linux-cef -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="$PWD/third_party/cef/linux64" -DCYBERDECK_REQUIRE_CEF=ON
cmake --build build-linux-cef --parallel
./build-linux-cef/cyberdeck-browser-cef "https://www.example.com"
```

Linux CEF code lives under `src/platform/linux`. Do not put Linux CEF hosting
inside the existing Win32 browser host.

## Download Helper

Use `scripts/download_cef.ps1` on Windows only with an official CEF download URL
that has been selected for this project:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_cef.ps1 -CefUrl "<official-cef-archive-url>"
```

Do not download or vendor CEF from mirrors or unknown repositories.

On Linux, prefer:

```bash
./scripts/install_linux.sh --deps
```

or pass a specific official archive URL:

```bash
CEF_URL="https://cef-builds.spotifycdn.com/<official-linux64-archive>.tar.bz2" \
  ./scripts/install_linux.sh --force-cef
```
