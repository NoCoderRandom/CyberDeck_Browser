# Third Party Notices

CyberDeck Browser is currently in early staged development. No third-party
source code or binary dependency has been vendored into this repository yet.

Before any third-party code, headers, binaries, generated files, media, fonts,
installer tools, or CEF runtime files are bundled with the project, this file
must be updated with:

- Component name.
- Version or commit.
- Official source URL.
- License name and license text or required notice location.
- Whether the component is bundled, generated, or installed externally.
- For CEF/Chromium builds, the exact codec-related build flags used for the
  distributed binary.
- Any attribution, redistribution, source-availability, or commercial-use
  requirements.

## Planned Components

See `docs/LICENSING_PLAN.md` for the planned dependency list and reuse rules.

## Reuse Rules

- Do not copy random CSS, shaders, 3D code, UI code, images, fonts, icons, or
  snippets from the internet unless the license is verified first.
- Any reused code must include attribution when the source license requires it.
- GPL-only dependencies are not allowed unless the project owner explicitly
  approves the license impact in writing.
- Prefer permissive licenses such as MIT, BSD, Apache-2.0, zlib/libpng, CC0,
  or public domain equivalents where legally usable.
