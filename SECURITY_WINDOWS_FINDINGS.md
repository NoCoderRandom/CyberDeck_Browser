# CyberDeck Browser Security Findings (Windows-focused)

This document summarizes security findings from the repository scan relevant to the Windows lane.
It is intentionally practical for handoff to implementation.

## Scope used for this review
- Project: `C:/Users/tipp_/OneDrive/Desktop/CyberDeck_Browser`
- Windows-owned lanes prioritized: `src/platform/windows`, `src/main`, `src/app`, `src/ui`, `scripts/*windows*`, installer flow.
- Shared files were also checked when they affect Windows runtime behavior.

## Findings (Windows impact and priority)

1. [High] CEF host sandbox is disabled at startup
- Location(s):
  - `src/browser/BrowserHost.cpp:1502`
  - `src/platform/linux/LinuxCefMain.cpp:2208` (shared behavior context)
  - `CMakeLists.txt:216` (`USE_SANDBOX OFF` for Linux config branch)
- Why it matters:
  - Browser process hardening is a core defense line against renderer/content escape.
  - Disabling it increases blast radius if a browser-side vulnerability is exercised.
- Windows feature impact:
  - Expected to improve containment. No direct change to CSS rendering features.

2. [Medium-High] Remote debugging port is controlled by environment variable
- Location:
  - `src/browser/BrowserHost.cpp:1505-1507`
  - `CYBERDECK_CEF_REMOTE_DEBUGGING_PORT`
- Why it matters:
 - Untrusted environment/launcher control can enable additional debug surface without explicit policy.
- Windows feature impact:
  - No relation to CSS manipulation features.

3. [High] Windows CEF download scripts lack integrity verification
- Location(s):
  - `scripts/build_windows_release.ps1:51-75`
  - `scripts/download_cef.ps1:22`
- Why it matters:
 - Remote archive fetch and extract is done without checksum/signature checks.
- Windows feature impact:
  - Affects packaging/release build trust, not runtime CSS behavior.

4. [High] Windows packaging cleanup can recursively delete user-directed paths
- Location:
  - `scripts/package_installer.ps1:152-163`
- Why it matters:
  - `Remove-Item -Recurse -Force` on unbounded path handling can remove unintended directories under misconfigured or malicious inputs.
- Windows feature impact:
  - No direct impact on UI/CSS feature handling.

5. [Medium] Build-system trust boundary via `CEF_ROOT`
- Location:
  - `CMakeLists.txt:129-133`, `144-148`, `206-220`
- Why it matters:
 - User/shell-provided CMake input path controls module load path and graph inclusion.
 - This is a build-time supply-chain trust boundary issue.
- Windows feature impact:
  - No runtime rendering or feature regression risk, but affects reproducibility and build integrity.

6. [Medium] Recursive user data JSON parsing has unbounded depth/size
- Location(s):
  - `src/deck/BookmarkNode.cpp:54-153`
  - `src/deck/BookmarkStore.cpp:52-152`
  - `src/history/HistoryStore.cpp:54-156`
- Why it matters:
 - A crafted corrupted local data file can cause resource exhaustion during load.
- Windows feature impact:
 - May affect startup behavior under corrupted history/bookmark files; does not target CSS processing logic.

7. [Medium-Low] Settings parser uses permissive extraction style
- Location:
  - `src/settings/SettingsStore.cpp:88-110`
- Why it matters:
 - Weak parsing shape increases malformed-input confusion risk.
- Windows feature impact:
 - Limited to settings parsing; not a CSS feature path.

## Non-reportable in this pass
- `SHR-render-001` (render/theme surface) was left as non-applicable for high-impact security boundaries in this review.

## Summary statement for coder
- The findings above are mostly hardening/build-chain and parsing boundaries.
- The CSS manipulation core is not called out as a direct breakage target by these issues.
- Fixes should be applied on launch/build/package boundaries and input validation, not in CSS engine code.
