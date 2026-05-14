# CyberDeck Browser Windows Fix Guide (Feature-Safe)

These fixes are written to preserve current product behavior, especially CSS and deck rendering features.

## Guiding rule
Do not gate, sanitize, or rewrite runtime CSS/DOM features.
Apply controls where trust boundaries exist:
- process startup configuration
- build/package supply-chain steps
- destructive filesystem operations
- malformed persisted-data parsing

## Fix set by priority (low risk of feature breakage)

1) Keep CSS feature behavior unchanged and isolate sandbox/control fixes from UI logic
- Keep `src/deck/*` rendering and `src/render/*` styling code untouched unless needed later.
- Apply browser hardening changes only in startup/config code.
- Add guard:
  - In debug/developer mode: optionally disable strictness if needed.
  - In release mode: enforce safer defaults.

2) Re-enable and constrain browser sandbox controls safely
- File: `src/browser/BrowserHost.cpp`
- Why this preserves features:
  - No CSS APIs changed.
  - Sandbox flag only alters process isolation when the Windows CEF bootstrap
    launch path is correctly wired.
- Suggested approach:
  - Keep `settings.no_sandbox = true` for the current executable packaging so
    portable and installer builds continue to launch.
  - Add an explicit startup log explaining that sandbox support is deferred until
    the Windows bootstrap/sandbox-info launch path is implemented.
  - Do not set `settings.no_sandbox = false` until `CefScopedSandboxInfo` or CEF
    bootstrap support is wired and tested.

3) Restrict remote debug activation
- File: `src/browser/BrowserHost.cpp`
- Why this preserves features:
  - No render pipeline changes.
- Suggested approach:
  - Keep `CYBERDECK_CEF_REMOTE_DEBUGGING_PORT` support.
  - Add checks before enabling:
    - port value in a bounded range
    - localhost-only binding requirement by default
    - optional explicit "allow remote debug" boolean in settings/launch policy.
  - Emit clear log if denied.

4) Add checksum/signature verification to Windows CEF acquisition
- Files: `scripts/build_windows_release.ps1`, `scripts/download_cef.ps1`
- Why this preserves features:
  - Purely build-time.
- Suggested approach:
  - Add expected SHA-256 manifest (version pinned).
  - Verify file hash after download before `Expand-Archive`.
  - Fail closed on mismatch.

5) Add safe deletion path checks in Windows packaging
- File: `scripts/package_installer.ps1`
- Why this preserves features:
  - Build artifact behavior remains the same when paths are valid.
- Suggested approach:
  - Resolve and normalize path.
  - Enforce cleanup only under known allowed root (`$ProjectDir`-derived temp tree or explicit whitelist).
  - Refuse `Remove-Item -Recurse -Force` when target is outside allowed base.

6) Tighten build integration trust without blocking local dev
- File: `CMakeLists.txt`
- Why this preserves features:
  - Does not touch runtime browser behavior.
- Suggested approach:
  - Require explicit `CEF_ROOT` provenance checks:
    - expected marker file
    - optional SHA manifest/signature metadata
  - Keep local developer override path documented for dev builds.

7) Add parser hardening with compatibility-first defaults
- Files:
  - `src/deck/BookmarkNode.cpp`
  - `src/deck/BookmarkStore.cpp`
  - `src/history/HistoryStore.cpp`
  - `src/settings/SettingsStore.cpp`
- Why this preserves features:
  - Add limits, not removals: parsing should still accept current normal files.
- Suggested approach:
  - Add depth/size/node-count caps with generous defaults (e.g., high enough for real users).
  - On exceed, fail parsing gracefully and log a recoverable warning.
  - Preserve existing fallback behavior so users do not lose styling/features.

## Rollout order (recommended)
1. Sandbox + remote debug hardening (runtime-safe, low break risk)
2. Packaging path safety (build/CI safety)
3. CEF download verification
4. Parser limits (with logging + metrics first, then strict enforcement)
5. CEF_ROOT/build integrity validation

## Validation checklist for your coder
- [ ] Confirm all existing CSS deck rendering features still load at startup.
- [ ] Confirm no layout/theme/stylesheet behavior change using current test scenes.
- [ ] Confirm Windows packaging still succeeds on clean build environment.
- [ ] Confirm browser startup remains compatible while sandbox bootstrap support is deferred.
- [ ] Confirm remote debug remains usable under explicit developer opt-in.
- [ ] Confirm malicious/incomplete JSON files no longer crash/hang and are handled gracefully.
- [ ] Confirm Windows package cleanup cannot delete outside approved workspace.

## Note
This is intended for the Windows lane only. Linux-equivalent notes can be produced separately if needed.
