# Real Traffic Fix: GTA III, Vice City, San Andreas

This fork contains a derivative, three-platform rework of Valdir da Costa
Junior's Real Traffic Fix. It has a shared traffic core and one game-specific
adapter for each game. It is not an official release by the original author.

## Contents

- `source/common`: shared traffic logic and platform contract.
- `source/platform`: GTA III, Vice City, and San Andreas adapters.
- `source/Main.cpp`: ASI entry point and optional autopilot permission export.
- `GTA_*/resources`: default INI files for the respective games.
- `premake5.lua` and `scripts`: Visual Studio project generation and build.

The repository contains source and default configuration files only. It does
not include installed game files, personal settings, logs, binaries, build
intermediates, or generated Visual Studio projects.

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
repository has not been independently rebuilt or play-tested. Please test each
game before treating this fork as a stable release.

The original Real Traffic Fix is by Valdir da Costa Junior. See `LICENSE` for
the MIT license and `THIRD_PARTY_NOTICES.md` for external build dependencies.
