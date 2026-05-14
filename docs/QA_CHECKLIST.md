# QA Checklist

Use this checklist for each CyberDeck Browser v1 release candidate. Record the
build identifier, CEF version, Windows version, GPU, and installer filename in
the release notes.

## Environment

- [ ] Test on a clean Windows 11 machine or VM.
- [ ] Install current GPU drivers where practical.
- [ ] Confirm the build was produced with the intended CEF distribution.
- [ ] Confirm `libcef.dll` and CEF resource files are present in the installer
  staging directory before compiling the installer.
- [ ] Run `scripts\verify_windows_media_runtime.ps1` against the installer
  staging app and the extracted portable app.
- [ ] Confirm whether the CEF distribution supports H.264/AAC. If it does not,
  release notes must list Reddit/MP4 media playback as a known limitation.

## Fresh Install

- [ ] Run `CyberDeckBrowserSetup-<version>.exe`.
- [ ] Accept the license page.
- [ ] Install to the default directory.
- [ ] Create Start Menu shortcut.
- [ ] Test both with and without the optional Desktop shortcut.
- [ ] Launch from the installer completion page.
- [ ] Launch from the Start Menu shortcut.

## Launch And Diagnostics

- [ ] App opens without crash.
- [ ] Default homepage loads when CEF is enabled.
- [ ] `SET` opens diagnostics.
- [ ] Diagnostics show app version, CEF state, data directory, settings path,
  log path, and OpenGL renderer details.
- [ ] `%APPDATA%\CyberDeckBrowser\logs\cyberdeck.log` is created.

## Browsing

- [ ] Navigate to `https://www.example.com`.
- [ ] Navigate to another HTTPS site used for smoke testing.
- [ ] Enter `example.com`; verify it normalizes to HTTPS.
- [ ] Enter a search phrase such as `cyberdeck browser test`; verify it falls
  back to DuckDuckGo search.
- [ ] Try a blocked unsafe scheme such as `javascript:alert(1)`; verify it is
  blocked and does not execute.

## Media Playback

- [ ] Toggle `TERM` off for media tests.
- [ ] Open `https://www.youtube.com/html5` and record reported codec support.
- [ ] Run `scripts\test_windows_media_playback.ps1` and confirm H.264/AAC
  support passes for codec-enabled releases.
- [ ] Play a normal YouTube video for at least 30 seconds.
- [ ] Play a Reddit-hosted video for at least 30 seconds.
- [ ] Confirm audio, pause/resume, seek, fullscreen, and tab switching.
- [ ] Check `%APPDATA%\CyberDeckBrowser\logs\cyberdeck.log` after any media
  failure.

## Tabs And Navigation

- [ ] Open a new tab.
- [ ] Switch between at least three tabs.
- [ ] Close a background tab.
- [ ] Close the active tab.
- [ ] Confirm at least one tab remains available.
- [ ] Navigate across multiple pages and test Back.
- [ ] Test Forward.
- [ ] Test Reload.
- [ ] Test Stop during an in-progress load where possible.

## Terminal Mode And Shell Effects

- [ ] Toggle `TERM` on and verify terminal styling is injected into the current
  page where allowed.
- [ ] Navigate with `TERM` on and verify styling applies to the new page.
- [ ] Toggle `TERM` off and verify future navigations are not styled.
- [ ] Toggle `SCAN`, `GLOW`, and `FLK`; verify only native shell effects change
  and normal page readability is preserved.

## Deck Space Vaults And Nodes

- [ ] Visit a normal HTTPS page and press `ADD NODE`.
- [ ] Confirm status message reports the Node was saved.
- [ ] Press `DECK` and enter Deck Space.
- [ ] Confirm the Vault Atlas starts with Search Array, AI Core, News Wire,
  Code Forge, and Media Bay on a readable outer circle.
- [ ] Confirm the selected Vault appears large in the center and also has a
  small selected-slot marker on the outer circle.
- [ ] Rotate Vault selection with mouse wheel and left/right arrows.
- [ ] Use `+` and `-` to zoom in and out without clipping the Vault Atlas.
- [ ] Enter a Vault with Enter and with double click.
- [ ] Leave a Vault with right click, Backspace, and Escape.
- [ ] Rename a Vault.
- [ ] Delete an empty Vault and confirm it is removed after confirmation.
- [ ] Confirm the saved Node appears with title label and `[FAV]` badge.
- [ ] Select a Node with mouse hover/click.
- [ ] Select Nodes with left/right keyboard navigation when multiple Nodes
  exist.
- [ ] Open a Node with Enter.
- [ ] Open a Node with double click.
- [ ] Confirm opening a Node creates a browser tab and updates visit metadata.
- [ ] Edit a Node title.
- [ ] Edit a Node URL and verify validation catches unsafe or empty input.
- [ ] Change Node shape and color.
- [ ] Delete a Node and confirm the red warning/confirmation appears.
- [ ] Restart the app and confirm edits/deletions persisted.

## Persistence And Recovery

- [ ] Change settings such as Terminal Mode or Deck layout.
- [ ] Restart and confirm settings persist.
- [ ] Add at least two Nodes in a Vault, restart, and confirm `bookmarks.json`
  reloads with Vault assignments intact.
- [ ] Back up `%APPDATA%\CyberDeckBrowser\settings.json`, write invalid JSON,
  launch, and verify a default settings file is created and the corrupt file is
  renamed.
- [ ] Repeat corrupted JSON recovery for `bookmarks.json`.
- [ ] Repeat corrupted JSON recovery for `history.json`.

## Installer Uninstall

- [ ] Uninstall from Windows Settings or Control Panel.
- [ ] Confirm program files and shortcuts are removed.
- [ ] Confirm user data under `%APPDATA%\CyberDeckBrowser` is preserved.
- [ ] Reinstall and confirm the app can still launch.
- [ ] Manually remove `%APPDATA%\CyberDeckBrowser` only after persistence checks
  are complete.

## Release Decision

- [ ] All blocker issues are fixed or documented.
- [ ] Known limitations are listed in README and release notes.
- [ ] Installer, README, license, and third-party notices are present.
- [ ] Release notes include exact build, package, and verification commands.
