# CyberDeck Browser — Windows Security Findings (Coder Handoff)

## Summary
This document contains security findings from the recent repository scan that matter for the Windows lane.

Important: These findings are focused on startup/build/package and persistence boundaries.  
They are **not** about your unique CSS/deck feature stack directly.

## High-priority findings to fix first

1) CEF sandbox disabled in host startup
- Severity: High
- Files:
  - `src/browser/BrowserHost.cpp:1502`
  - `src/platform/linux/LinuxCefMain.cpp:2208` (context for shared behavior)
- Issue: `no_sandbox` is forced on in places where hardening should usually stay enabled.
- Risk: Increases renderer/CEF content compromise impact and weakens isolation.

2) Remote debugging can be enabled by environment without strict policy
- Severity: Medium-High
- File: `src/browser/BrowserHost.cpp:1505-1507`
- Issue: `CYBERDECK_CEF_REMOTE_DEBUGGING_PORT` is used directly for remote debugging.
- Risk: Increases local debug surface if launch environment is uncontrolled.

3) Windows CEF download scripts have no integrity verification
- Severity: High
- Files:
  - `scripts/build_windows_release.ps1:51-75`
  - `scripts/download_cef.ps1:22`
- Issue: Remote archive download + extract without checksum/signature check.
- Risk: Supply-chain compromise in build chain.

4) Windows package cleanup deletes user-controlled paths recursively
- Severity: High
- File: `scripts/package_installer.ps1:152-163`
- Issue: Recursive delete path is not strongly allowlisted before cleanup.
- Risk: Wrong path input can remove unintended directories.

5) Build-time trust boundary via `CEF_ROOT`
- Severity: Medium
- File: `CMakeLists.txt:129-133`, `144-148`, `206-220`
- Issue: External path is directly injected into CEF module/subdirectory wiring.
- Risk: Untrusted CEF root can influence build execution graph.

## Medium findings to fix next

6) Unbounded parsing of persisted JSON (bookmarks/history)
- Severity: Medium
- Files:
  - `src/deck/BookmarkNode.cpp:54-153`
  - `src/deck/BookmarkStore.cpp:52-152`
  - `src/history/HistoryStore.cpp:54-156`
- Issue: Recursive parse without robust depth/size quotas.
- Risk: Local malformed/corrupted file DoS during load.

7) Permissive settings field extraction
- Severity: Medium-Low
- File: `src/settings/SettingsStore.cpp:88-110`
- Issue: String-search style extraction is less robust than stricter schema parsing.
- Risk: Malformed settings input handling robustness issues.

## What is not in this list (for now)
- No reportable renderer/core-theme CSS breakage was identified in this pass.
- No direct evidence that these issues require changing your CSS manipulation logic.

## Delivery note
If you only patch one area first, start with items 1–4 because they provide the largest security gain with minimal feature risk.
