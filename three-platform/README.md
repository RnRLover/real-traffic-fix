# Real Traffic Fix: GTA III, Vice City, San Andreas

Source snapshot for review and possible publication by the original Real Traffic Fix author.
This is a derivative, three-platform version with a shared traffic core and one
game-specific adapter for each game. It is not an official release by the author.

## Contents

- `source/common`: shared traffic logic and platform contract.
- `source/platform`: GTA III, Vice City, and San Andreas adapters.
- `source/Main.cpp`: ASI entry point and optional autopilot permission export.
- `GTA_*/resources`: default INI files for the respective games.
- `premake5.lua` and `scripts`: Visual Studio project generation and build.

No installed game files, personal settings, logs, binaries, build intermediates,
or machine-specific generated Visual Studio projects are included.

## Build

Requires Windows, Visual Studio with the v143 C++ toolset, Premake 5, and
DK22Pac/plugin-SDK with its usual libraries and DirectX dependencies built.
Set `PLUGIN_SDK_DIR` to the plugin-SDK root and run from this directory:

```bat
set PLUGIN_SDK_DIR=C:\path\to\plugin-sdk
scripts\generate_vs2022.bat
scripts\build_release.bat
```

The three Win32 Release ASIs and matching INIs are written under
`output\asi\<project>\Release`. The projects are `GTA3RealTrafficFix`,
`GTAVCRealTrafficFix`, and `GTASARealTrafficFix`.

## Status and attribution

On 2026-09-20, the release ASIs built from the source directory used for this
snapshot matched the versions installed in the three games byte-for-byte. This
archive has not been independently rebuilt or play-tested. Please review and
test it in each game before publication.

The original Real Traffic Fix is by Valdir da Costa Junior. See `LICENSE` for
the MIT license and `THIRD_PARTY_NOTICES.md` for external build dependencies.
