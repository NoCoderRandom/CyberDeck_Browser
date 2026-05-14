# CyberDeck Browser Release Notes

## Version

- Version:
- Release date:
- Installer:
- Commit or source snapshot:
- CEF version:
- CEF codec build flags:
- Windows test version:

## Summary

Briefly describe the release in user-facing terms. Do not claim production
readiness unless the release has completed the full QA checklist and security
review.

## Highlights

- Native Windows 11 browser shell.
- CEF-backed website rendering.
- CyberDeck terminal visual theme.
- Deck Space 3D bookmark Nodes.
- Local settings, history, bookmarks, favicons, and logs.
- Windows installer packaging.

## Changes

- Added:
- Changed:
- Fixed:
- Removed:

## Verification

Build commands:

```powershell

```

Run commands:

```powershell

```

Packaging commands:

```powershell

```

QA performed:

- [ ] Fresh install
- [ ] Launch
- [ ] HTTPS browsing
- [ ] CEF runtime verifier
- [ ] YouTube HTML5 codec report
- [ ] YouTube video playback
- [ ] Reddit video playback
- [ ] URL normalization/search fallback
- [ ] Tabs
- [ ] Back/forward/reload
- [ ] Terminal Mode
- [ ] Add Node
- [ ] Deck Space entry
- [ ] Open/edit/delete Node
- [ ] Settings persistence
- [ ] Corrupted JSON recovery
- [ ] Installer uninstall

## Known Limitations

- 

## Upgrade Notes

- User data is stored under `%APPDATA%\CyberDeckBrowser`.
- Uninstall preserves user data unless manually removed.

## Third-Party Notices

- CEF/Chromium notices must be reviewed for the exact CEF distribution used.
- Keep `LICENSE` and `THIRD_PARTY_NOTICES.md` in the installer package.
