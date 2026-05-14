# CyberDeck Browser v0.1.0-rc3 Release Notes

## Version

- Version: `0.1.0-rc3`
- Release date: 2026-05-02
- Tag: `v0.1.0-rc3`
- Portable asset: `CyberDeckBrowser-0.1.0-rc3-portable-win64.zip`
- Installer asset: not produced in this environment unless Inno Setup is
  available on the packaging machine.
- CEF version: default Windows CEF archive
  `cef_binary_147.0.10+gd58e84d+chromium-147.0.7727.118_windows64`
- CEF codec build flags: default official CEF distribution, not marked
  codec-enabled.

## Summary

This Windows release candidate focuses on Deck Space usability, safer resource
cleanup, and portable packaging. Deck bookmarks now live inside editable Vaults,
the Vault Atlas has a larger circular layout with a centered selected Vault and
an outer-slot marker, and the Windows toolbar/readability pass makes the main
browser chrome easier to use.

## Highlights

- Added Deck Vaults as first-class bookmark containers.
- Seeded fresh profiles with five default Vaults and 19 starter Nodes.
- Added Vault rename/delete workflows while keeping child Nodes safe.
- Added Vault Atlas rotation, larger orbit spacing, selected-slot marker, and
  `+`/`-` keyboard zoom.
- Increased top toolbar, address bar, tab, and Deck overlay text sizing.
- Added a real `CyberdeckPortable.exe` launcher for portable builds.
- Improved Windows OpenGL resource cleanup during Deck Space shutdown.
- Fixed bookmark storage handling around legacy empty files.

## Verification

Build commands used:

```powershell
git fetch --prune origin --tags
cmake --build build-windows-release --config Release
ctest --test-dir build-windows-release -C Release --output-on-failure
.\scripts\build_windows_release.ps1 -Version "0.1.0-rc3" -SkipInstaller -SkipMediaProbe
```

Additional portable smoke checks:

```powershell
.\scripts\verify_windows_media_runtime.ps1 -AppDir "dist\release-staging\CyberDeckBrowser-0.1.0-rc3-portable-win64\App"
```

Manual/automated smoke coverage included:

- Release build completed.
- CTest suite passed.
- CEF runtime staging verifier passed.
- Portable EXE launcher started the app with local `Data`.
- Deck Space opened.
- Vault defaults seeded as 5 Vaults and 19 Nodes.
- `+` and `-` zoom key paths were exercised in Deck Space.

## Known Limitations

- This is still a release-candidate build, not a hardened production browser.
- The Windows build is not code-signed.
- This environment did not run a clean Windows VM install/uninstall pass.
- Inno Setup was not available here, so the GitHub release may contain only the
  portable zip unless an installer is built on another packaging machine.
- The default official CEF distribution is not marked codec-enabled. Do not
  claim Reddit/H.264/AAC playback support until a licensed codec-enabled CEF
  build passes media QA.
- Real favicon capture from CEF is not implemented yet; Deck Space uses local
  placeholder favicon badges.
- Deck Space thumbnails are not implemented.
- A full production security audit is still pending.

## Upgrade Notes

- Normal builds keep user data under `%APPDATA%\CyberDeckBrowser`.
- Portable builds keep user data under the package-local `Data` folder when
  started through `CyberdeckPortable.exe` or `CyberDeckBrowserPortable.cmd`.
- Existing flat Nodes remain supported. Nodes with `vaultId` render inside their
  Vault; loose Nodes are preserved if a Vault is deleted.

## Third-Party Notices

- CEF/Chromium notices must be reviewed for the exact CEF distribution used.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, and `README.md` are included in the
  portable package and installer staging directory.
