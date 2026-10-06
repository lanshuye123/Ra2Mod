# HAres

A Syringe/SyringeEx code-injection extension for **Command & Conquer: Yuri's Revenge** (`gamemd.exe`),
designed to run **alongside Ares 3.0 and Phobos**.

It exists because Ares' public source code stops at release **0.A (2016-01-02)** while the Ares installed
in the game directory is the **closed-source 3.0**. Forking Ares 0.A would mean giving up nine years of
features, so HAres takes the same route Phobos took: a separate hook DLL that patches only what it needs to.

`docs/DEVELOPMENT.md` is the full development guide (in Chinese). Read it before writing hooks.

## Status

Verified on this machine: builds clean with MSVC v143 (VS2022), is recognised by both the original
Syringe 0.7.3.0 and a locally built SyringeEx 0.1.0.2, and runs alongside Ares 3.0 and Phobos in a
real game session. The game reaches the main menu and four of the five demo hooks have been observed
executing (`ExeRun`, `YR_CmdLineParse`, `ScenarioClass::Start`, `GScreenClass_DrawText`); the fifth
(`ExeTerminate`) only runs on a clean process exit. See the verification section of the development
guide for the raw log evidence.

## Requirements

- Visual Studio 2022 (or VS Build Tools 2022) with `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`
- The `YRpp` submodule, pinned to the commit Phobos is validated against

## Building

```bat
git submodule update --init --recursive

scripts\build_debug.bat      :: -> Debug\HAres.dll   + HAres.pdb
scripts\build_release.bat    :: -> Release\HAres.dll + HAres.pdb
```

If the submodule clone fails, route GitHub through the mirror first:

```bat
git config url."https://ghfast.top/https://github.com/".insteadOf "https://github.com/"
git submodule update --init --recursive
```

## Deploying and running

```bat
scripts\deploy.bat Release D:\Games\Ra2
```

That copies `HAres.dll` and `RunHAres.bat` next to `gamemd.exe` and creates a default
`HAres.ini` if you do not have one. Then just run the deployed launcher:

```bat
D:\Games\Ra2\RunHAres.bat
```

which expands to:

```bat
Syringe.exe "gamemd.exe" --handshakes --args="-WIN -CD -NOLOGO -LOG -AI-CONTROL"
```

Note the argument style: **SyringeEx requires game arguments after `--args="..."`.** Anything else on
the command line is treated as one of Syringe's own options and never reaches the game, which silently
loses `-LOG` (no `debug.log`) and `-WIN` (the game then dies at DirectDraw `CreateSurface 80070057`).
The original closed-source Syringe instead wants them as trailing arguments - `RunHAres.bat /oldsyringe`
switches back to that form.

Logs to inspect:

| File | Contents |
|---|---|
| `HAres.log` | This DLL's own log, flushed after every line so it survives a crash |
| `syringe.log` | DLL recognition, handshakes, hook count, exceptions |
| `debug\debug.log` | Ares/Phobos log (needs `-LOG`) |

`scripts\restore_game_dir.bat` restores the game directory from `_dsh_backup_orig\`.

## Configuration

`HAres.ini`, next to `gamemd.exe`:

```ini
[General]
ShowWatermark=1        ; draw the version banner in game
VerboseLog=0           ; extra diagnostics, including a patch-machinery self test
WatermarkCorner=0      ; 0=top-left 1=top-right 2=bottom-left 3=bottom-right
```

## Adding code

- Hooks live in `src\Misc\`; game-class extensions belong in `src\Ext\<ClassName>\`.
- Any new `.cpp` must be added to `HAres.vcxproj` under `<ClCompile>`, or it will not be compiled.
- Export names must be unique across the process; prefix them with `HAres_`.
- Do not change the ABI-critical compiler options in `HAres.props` - see section 1.4 of the guide.

## Features

Each feature has its own document under [`docs/functions/`](docs/functions/) describing what it does,
how to use it, why it is implemented that way, and how the Ares/engine addresses it hardcodes were
derived. General development practice lives in the guide.

| Feature | Doc | Depends on |
|---|---|---|
| Unit-provided superweapons | [unit-superweapons.md](docs/functions/unit-superweapons.md) | Ares 3.0 |
| Area promotion/demotion superweapon (`Type=PromoteAura`) | [promote-aura-superweapon.md](docs/functions/promote-aura-superweapon.md) | Ares 3.0 |
| Change type on promotion (`Promote.VeteranType` / `EliteType`, health/star policy) | [promotion-convert.md](docs/functions/promotion-convert.md) | Ares 3.0 |
| Mind control shield zones (`MindControlShield.*`) | [mind-control-shield.md](docs/functions/mind-control-shield.md) | - |

## Research

Feasibility studies for features that are **not** implemented, in [`docs/research/`](docs/research/).
Each records the evidence (addresses, file/line references) and separates verified facts from
inference.

| Study | Doc | Verdict |
|---|---|---|
| LLM-driven AI (`aimd.ini`) | [feasibility-llm-ai-and-modern-formats.md](docs/research/feasibility-llm-ai-and-modern-formats.md) | Feasible only as an **offline content generator**; in-match calls cannot work under YR's lockstep + RNG-synchronised model |
| Modern asset formats (PNG/GIF/GLB for SHP/VXL) | same document | Feasible only as **authoring formats with a build-time conversion**; the engine cannot read them directly |

## Layout

```
src/HAres.cpp                 lifecycle, config, DllMain
src/Misc/Hooks.Demo.cpp       DEFINE_HOOK entries and the hook template
src/Misc/UnitSuperWeapon.*    unit-provided superweapons: INI registry and queries
src/Misc/AresUnitSuperWeapon.cpp  the Ares 3.0 call-site patch
src/Misc/AresHelpers.*        Ares module/version lookup and Ares' ConvertTypeTo
src/Misc/PromoteAura.*        Type=PromoteAura superweapon: area promotion/demotion
src/Misc/PromoteConvert.*     change type on promotion (Promote.*Type)
src/Misc/MindControlShield.*  mind control shield zones
src/Misc/SharedUtils.h        house filters, cell-to-lepton ranges, INI helpers
src/Utilities/                logging, patch engine, patch macros
docs/DEVELOPMENT.md           general development guide
docs/functions/               one document per implemented feature
docs/research/                feasibility studies for unimplemented ideas
scripts/                      build, deploy, restore
YRpp/                         game binary type definitions (submodule)
```
