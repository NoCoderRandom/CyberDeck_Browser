# Windows Packaging

CyberDeck Browser uses Inno Setup for Windows installer packaging.
Linux packaging is intentionally separate and is not implemented yet.

## Build Release Binaries

Recommended one-command Windows release build:

```powershell
.\scripts\build_windows_release.ps1 -SkipInstaller
```

That script downloads the default official Windows CEF binary distribution when
`-CefRoot` is not supplied, builds CyberDeck Browser with CEF required, stages
installer files, verifies the CEF runtime, and creates a portable zip under
`dist\release-assets`.

If you do not trust the implicit manifest path, pass the checksum explicitly:

```powershell
.\scripts\build_windows_release.ps1 -SkipInstaller -CefSha256 "<trusted sha256>"
```

For release candidates, pass the release identifier explicitly so generated
portable archives match the Git tag:

```powershell
.\scripts\build_windows_release.ps1 -Version "0.1.0-rc3" -SkipInstaller
```

To build with a codec-enabled CEF distribution that you have licensed and trust:

```powershell
.\scripts\build_windows_release.ps1 `
  -CefUrl "https://example.com/cef_binary_<codec-enabled>_windows64.tar.bz2" `
  -CodecEnabledCef `
  -AcceptCodecResponsibility `
  -CefSha256 "<trusted sha256>"
```

or:

```powershell
.\scripts\build_windows_release.ps1 `
  -CefRoot "C:\path\to\codec-enabled-cef" `
  -CodecEnabledCef `
  -AcceptCodecResponsibility
```

Use `-SkipInstaller` when Inno Setup is not installed or when you only need the
portable zip and installer staging folder.

When `-CodecEnabledCef` is supplied, the release helper also runs
`scripts\test_windows_media_playback.ps1` unless `-SkipMediaProbe` is supplied.

Manual release build:

```powershell
.\scripts\build_release.ps1
```

For a CEF-enabled release build, pass the extracted CEF binary distribution:

```powershell
.\scripts\build_release.ps1 -CefRoot "C:\path\to\cef_binary" -RequireCef
```

The CMake CEF integration copies required CEF runtime files into the build
output when CEF is configured with a compatible MSVC generator. The installer
script stages everything next to `CyberDeckBrowser.exe`, so CEF runtime files
must already be present in the release build output before packaging.

Media support depends on the selected CEF build. Reddit and many YouTube
streams require H.264/AAC support, which is not guaranteed by the default CEF
binary distribution. See [Windows Media Playback](WINDOWS_MEDIA.md) before
uploading release assets.

## Package Installer

Install Inno Setup 6 on the developer machine, then run:

```powershell
.\scripts\package_installer.ps1
```

If `ISCC.exe` is not on `PATH`, pass it explicitly:

```powershell
.\scripts\package_installer.ps1 -IsccPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
```

The generated installer is written to `dist\release-assets` using the version
from `installer\CyberDeckBrowser.iss`, for example
`CyberDeckBrowserSetup-0.1.0-rc3.exe`.
Use `-SkipCompile` to prepare and inspect `dist\installer-staging\app` without
requiring Inno Setup:

```powershell
.\scripts\package_installer.ps1 -SkipCompile
```

The packaging script fails when required CEF runtime files are missing. To
intentionally package the non-CEF placeholder build, pass `-AllowPlaceholder`.

To verify an extracted portable package or staging directory:

```powershell
.\scripts\verify_windows_media_runtime.ps1 -AppDir "dist\installer-staging\app"
```

The portable package contains `CyberdeckPortable.exe` as the primary launcher.
It starts `App\CyberDeckBrowser.exe` and sets `CYBERDECK_APPDATA_DIR` so the
CEF profile/cache, settings, history, bookmarks, favicons, and logs stay inside
the portable package's `Data` folder. `CyberDeckBrowserPortable.cmd` is kept as
a transparent fallback/debug launcher.

## Installer Behavior

- Installs program files under Program Files when elevated, with Inno Setup's
  user-level install fallback available.
- Creates a Start Menu shortcut.
- Offers an optional Desktop shortcut.
- Supports normal Windows uninstall.
- Does not delete user data under `%APPDATA%\CyberDeckBrowser`; bookmarks,
  settings, history, favicons, and logs remain unless a future explicit cleanup
  option is added.

## Release Checklist

- Build with the intended CEF runtime and verify `libcef.dll` is staged.
- For release candidates, build with `-Version "<version-id>"` and confirm the
  portable zip name matches the tag.
- Run `scripts\verify_windows_media_runtime.ps1` against the staged installer
  app and the extracted portable app.
- Run `scripts\test_windows_media_playback.ps1` against the built executable
  before claiming Reddit/MP4 video support.
- Confirm whether the selected CEF build supports H.264/AAC. If it does, run
  YouTube and Reddit video smoke tests. If it does not, document that limitation
  in the release notes.
- Confirm `LICENSE`, `THIRD_PARTY_NOTICES.md`, and `README.md` are in the
  installer staging directory.
- Recheck CEF/Chromium notices before public distribution.
- Test install, launch, and uninstall on a clean Windows 11 VM.

## Linux Packaging Status

The Linux build currently produces the separated `cyberdeck-browser` core
launcher only. Future AppImage, deb, rpm, or Flatpak packaging should live in a
Linux-specific packaging path and should not modify the Inno Setup workflow.
