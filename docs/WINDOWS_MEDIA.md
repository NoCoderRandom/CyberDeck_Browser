# Windows Media Playback

CyberDeck Browser uses CEF/Chromium for normal website media. If regular pages
load but Reddit or YouTube videos do not play, check both of these areas before
publishing a Windows release:

1. The packaged app must include the full CEF runtime.
2. The CEF build must support the codecs the site serves.

## CEF Runtime Files

Run this against the installer staging directory or an extracted portable app:

```powershell
.\scripts\verify_windows_media_runtime.ps1 -AppDir "dist\installer-staging\app"
.\scripts\verify_windows_media_runtime.ps1 -AppDir "dist\release-staging\CyberDeckBrowser-<version>-portable-win64\App"
```

The verifier checks for `libcef.dll`, `chrome_elf.dll`, CEF PAK/data files,
GPU helper DLLs, and `locales\en-US.pak`. The installer packaging script runs
the same required-file check and now fails unless CEF is present, unless
`-AllowPlaceholder` is passed intentionally.

Run the in-browser media codec probe against a built Windows executable:

```powershell
.\scripts\test_windows_media_playback.ps1 -AppExe "build-windows-release\Release\CyberDeckBrowser.exe"
```

The probe launches CyberDeck, serves a local test page, and fails the script if
CEF does not report H.264/AAC support. Codec-enabled release builds run this
probe automatically unless `-SkipMediaProbe` is supplied.

## H.264 And AAC

Many Reddit videos and some YouTube streams are served as MP4/H.264 video with
AAC audio. Chromium lists H.264/AVC and AAC as proprietary codecs limited to
Google Chrome. The CEF project also notes that default CEF/Chromium builds have
proprietary codecs disabled because of patent/licensing requirements.

That means the normal official CEF binary can load the sites while still being
unable to decode some video streams. Packaging files correctly is necessary,
but it is not enough for H.264/AAC.

To support those streams in a distributed Windows build:

- Build CEF from official source with proprietary codec support enabled, for
  example with GN arguments such as `proprietary_codecs=true` and
  `ffmpeg_branding=Chrome`.
- Clear the required codec licensing before distributing that build.
- Rebuild CyberDeck Browser with that codec-enabled `CEF_ROOT` or trusted
  codec-enabled CEF archive URL.
- Repackage both installer and portable releases from that build output:

```powershell
.\scripts\build_windows_release.ps1 `
  -CefRoot "C:\path\to\codec-enabled-cef" `
  -CodecEnabledCef `
  -AcceptCodecResponsibility
```

or:

```powershell
.\scripts\build_windows_release.ps1 `
  -CefUrl "https://example.com/cef_binary_<codec-enabled>_windows64.tar.bz2" `
  -CodecEnabledCef `
  -AcceptCodecResponsibility
```

- Keep the exact CEF/Chromium version, build flags, source URL, and license
  notes in `THIRD_PARTY_NOTICES.md` and the release notes.

Do not swap in random `libcef.dll`, FFmpeg, or codec DLLs from another project.
CEF runtime files must come from one consistent CEF build.

## Windows Smoke Tests

Run these before uploading Windows release assets:

- Open `https://www.youtube.com/html5` and confirm H.264 support is reported
  when releasing a codec-enabled build.
- Run `scripts\test_windows_media_playback.ps1` and confirm H.264/AAC passes.
- Play a normal YouTube video for at least 30 seconds.
- Play a Reddit-hosted video for at least 30 seconds.
- Toggle `TERM` off during media tests to rule out site styling interference.
- Confirm audio, pause/resume, seek, fullscreen, and tab switching.
- Check `%APPDATA%\CyberDeckBrowser\logs\cyberdeck.log` after failures.

If H.264/AAC licensing has not been cleared, document Reddit/MP4 media playback
as a known limitation instead of shipping it as supported.
