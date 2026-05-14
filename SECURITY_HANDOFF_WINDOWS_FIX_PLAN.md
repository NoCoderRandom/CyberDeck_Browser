# CyberDeck Browser — Windows Non-Breaking Fix Plan (Coder Handoff)

## Objective
Fix security issues without disturbing existing unique features (CSS, deck rendering, and runtime behaviors).

## Guardrails (must keep feature behavior)
- Do not change deck rendering pipelines unless explicitly required later.
- Do not add strict CSS/parser policy in rendering layers.
- Keep existing behavior as baseline; apply guards and explicit opt-ins only.
- If uncertain, log and fallback safely rather than hard-failing user flow.

## Phase 1 (safe, high priority)

1) Harden CEF startup flags in Windows entry path
- File: `src/browser/BrowserHost.cpp`
- Change: do not silently claim sandbox support until Windows bootstrap/sandbox-info
  startup is wired.
- Keep current executable startup behavior intact for the portable and installer
  builds.
- Implementation idea:
  - Keep `settings.no_sandbox` true for the current executable packaging.
  - Add clear log reason while this compatibility mode is active.
  - Re-enable sandboxing in a separate bootstrap/DLL packaging change with
    `CefScopedSandboxInfo`/CEF bootstrap support.
- Test: browser starts and loads current pages/decks normally.

2) Restrict remote debug environment control
- File: `src/browser/BrowserHost.cpp`
- Change: keep env var support, add policy checks.
- Policy checks:
  - validate port range
  - require safe binding mode by default (localhost)
  - require explicit opt-in switch for non-loopback/listen broad.
- Test: no behavior change for expected launch path; safe rejection is logged for risky config.

3) Add checks for Windows CEF archive download integrity
- Files: `scripts/build_windows_release.ps1`, `scripts/download_cef.ps1`
- Change: verify checksum/signature before extraction.
- Recommended:
  - maintain per-version manifest/expected hash
  - compare hash after download
  - abort packaging on mismatch
- Test: known-good download passes; tampered file fails fast before use.

## Phase 2 (still low feature risk)

4) Constrain Windows packaging cleanup paths
- File: `scripts/package_installer.ps1`
- Change: add allowlist / canonical path validation before `Remove-Item -Recurse -Force`.
- Guard:
  - normalize target path
  - ensure target resides under approved staging root
  - refuse and fail loudly otherwise.
- Test: normal build path still works; dangerous/foreign paths are blocked.

5) Bound CMake external root trust check
- File: `CMakeLists.txt`
- Change: validate `CEF_ROOT` inputs before module path injection and subdirectory.
- Options:
  - expected marker file validation
  - documentation for verified local source tree path
  - CI path checks.
- Test: local legit build succeeds; invalid/untrusted root rejected.

## Phase 3 (stability hardening, no feature regression if conservative)

6) Add controlled limits to persisted JSON parsing
- Files:
  - `src/deck/BookmarkNode.cpp`
  - `src/deck/BookmarkStore.cpp`
  - `src/history/HistoryStore.cpp`
- Change: depth/size/node-count caps.
- Keep caps high enough for normal users; malformed files fallback gracefully with safe recovery.
- Test: normal bookmark/history imports unaffected.

7) Harden settings parser extraction progressively
- File: `src/settings/SettingsStore.cpp`
- Change: tighten parser path with schema-aware fallback handling.
- Behavior: reject malformed settings safely while keeping default values available.
- Test: old valid settings continue to load.

## Rollout order (recommended)
1. Steps 1, 2, 3, 4, 5 (high impact)
2. Steps 6, 7 (reliability hardening)

## Suggested execution pattern
- Patch one area at a time.
- After each step, run normal startup + core feature smoke path:
  - open bookmarks/history
  - load a deck scene
  - apply core CSS features
- If behavior changes, keep the security default on and gate stricter checks behind explicit opt-in first.

## Acceptance criteria
- No intentional change to core visual or feature behavior.
- Security-sensitive defaults are safer by default.
- Risky destructive or supply-chain paths now have explicit checks/fail-close behavior.
