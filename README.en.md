# KardsFactionToolkit

[简体中文](README.md) | English

A source toolkit for building a DLL that adds factions to KARDS on Windows x64. The default configuration adds **Romania as an allied nation: `Romania = 13`, deck code `b`**. Edit the JSON configuration and rebuild to include multiple new allied nations in a single `dwmapi.dll`.

This standalone C++ project is based on the working `RomaniaAllyNative 0.1.1` implementation. Building it does not require Unreal Engine or UE4SS, and the runtime does not use Lua. The DLL extends the faction enum and three native conversion functions at game startup. Faction names, icons, cards, and UI elements are still configured through Unreal Engine assets.

## Features

- Preserve the existing faction values `0..12` and append new factions starting at `13`.
- Extend faction name → enum value, enum value → deck character, and deck character → enum value conversions.
- Configure factions in `config/factions.json`, with validation for duplicate names, duplicate codes, and invalid values.
- Verify PE version fields, critical function bytes, and the original enum structure. Renamed executables such as `1.exe` are supported when their relevant build data matches; **a matching whole-file SHA256 is not required**.
- Discover Visual Studio, generate forwarding code, compile, run isolated tests, and produce the DLL.
- Create a source-only ZIP suitable for publishing on GitHub.

The current scope is **adding allied nations**. Main-nation features such as headquarters, card backs, progression, and default decks require additional work. The DLL does not create faction buttons or change server rules. Acceptance of new factions and deck codes by a server must be verified in the intended environment.

## Quick build

Requirements:

1. Windows x64 and Visual Studio 2022 or 2026 with **Desktop development with C++**, the MSVC x64 toolchain, Windows SDK, and **C++ CMake tools for Windows**.
2. **64-bit Python 3.10 or newer**.
3. A local KARDS executable matching a supported build profile. Game files are not included.

Open PowerShell in the project root:

```powershell
python -m pip install -r requirements.txt

.\Build.ps1 -GameExe 'D:\kards\KARDS\default\game\kards\Binaries\Win64\1.exe'
```

Replace the example path with your installation path. No game installation path is hardcoded in the source. You can also use `kards-Win64-Shipping.exe`.

A successful build produces:

```text
dist/
  dwmapi.dll             Runtime DLL
  manifest.json          Faction configuration, build profile, and input hash
  SHA256SUMS.txt          DLL checksum
  README.md              Chinese documentation
  README.en.md           English documentation
  LICENSE
  THIRD_PARTY_NOTICES.md
  licenses/MinHook.LICENSE.txt
  docs/                  Additional documentation
```

The build script writes to the project output directories; **it does not deploy files to the game**. If tests fail, the script stops before installing the new DLL into `dist`. Files from an earlier successful build may remain there, so check the current command result and checksum.

If your local PowerShell policy blocks the script, you can set the policy for that invocation only:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -GameExe 'D:\YourGameDirectory\1.exe'
```

## Configure new factions

The default `config/factions.json` contains:

```json
{
  "schema_version": 1,
  "factions": [
    {"name": "Romania", "display_name": "罗马尼亚", "value": 13, "deck_code": "b"}
  ]
}
```

| Field | Meaning and requirements |
| --- | --- |
| `name` | Internal faction name. An ASCII identifier starting with a letter, followed by letters, digits, or underscores; maximum 48 characters. Runtime name matching is case-insensitive. |
| `display_name` | A display label recorded in the build manifest. Unicode text is supported. **It is not automatically added to the game's name table or localization data.** |
| `value` | Native enum value. Values must be consecutive in configuration order, starting at 13. Existing values, including `Allies=11` and `Neutral=12`, remain unchanged. |
| `deck_code` | One lowercase character from `b..z`, unique within the configuration. `0..9` and `a` are reserved for the original game. |

For example, to add a second allied nation:

```json
{
  "schema_version": 1,
  "factions": [
    {"name": "Romania", "display_name": "Romania", "value": 13, "deck_code": "b"},
    {"name": "Hungary", "display_name": "Hungary", "value": 14, "deck_code": "c"}
  ]
}
```

Put all new factions in **one DLL**, rather than installing a separate DLL with the same filename for each nation. The configuration supports up to 25 new entries; full game UI support has not been verified for that upper limit. Keep values and codes stable once they are used by assets or saved decks, because changing them changes how existing data is interpreted.

`config/example.two-allies.json` contains the same two-faction setup with Chinese display labels. Pass it through `-FactionConfig` to try it. The default configuration still contains only Romania.

To select a configuration explicitly:

```powershell
.\Build.ps1 -GameExe 'D:\YourGameDirectory\1.exe' -FactionConfig '.\config\factions.json'
```

## Install, inspect logs, and uninstall

1. Exit the game.
2. Copy `dist/dwmapi.dll` into **the directory containing the game executable you actually launch**, usually `kards/Binaries/Win64`.
3. If another tool already uses `dwmapi.dll` in that directory, identify and back up that file first. This project does not implement proxy chaining; two files cannot occupy the same path. Do not copy this DLL into a Windows system directory.
4. Launch the game normally. The DLL writes `KardsFactionToolkit.log` beside itself.

A successful installation with the default configuration includes these messages:

```text
READY: EFactionEnum::Romania=13, deck code=b; original faction values 0..12 preserved
READY: early enum registration and all three native deck conversions installed
```

Keep the filename `dwmapi.dll`: the supported game build imports system DWM APIs under that name. The proxy forwards those exports to the original Windows library in `System32`. Renaming the file prevents this automatic loading mechanism from working.

`READY` confirms installation of the native extension. It does not confirm that the UI, card tables, deck saving, or server integration work. For an incompatible executable, faction extension is skipped. A rejection during the initial entry check may produce no log; DWM forwarding remains available.

To uninstall, exit the game, remove this tool's `dwmapi.dll`, and restore any previous file with that name. Do not unload the module while the game is running: Unreal Engine's enum names and function detours still reference its memory.

## Required Unreal Engine assets

The DLL handles native faction recognition. A complete faction also needs:

- The same internal names and numeric values in the editor's `EFactionEnum`.
- Entries in `DT_FactionNames`, `DT_FactionIconTextures`, and any sound or appearance tables actually referenced by the game.
- Updates to faction ranges, allied-nation lists, and default branches in `FactionFunctions`, `UtilityFunctions`, and `DeckAndCollectionUtilityFunctions`.
- Appropriate `W_CreateDeck` and `selectFactionButton_Widget` instances for deck creation.
- Collection and deck-building filters in `W_CollectionFilters` and `W_DeckBuilder_FilterWidget`. Updating one entry point does not ensure that the icon appears in both interfaces.
- Card faction assignments, asset registry entries, and packaged assets.

See [UE integration notes](docs/UE_INTEGRATION.md) (Chinese). Game tables, Blueprints, icons, and modified game assets are not included in this source project.

## Supported game build

Bundled profile: [`profiles/kards-20261002-win64.json`](profiles/kards-20261002-win64.json).

| PE field | Verified value |
| --- | --- |
| Machine | AMD64 / `0x8664` |
| TimeDateStamp | `0xfe01dfb1` |
| SizeOfImage | `0x9cc4000` |
| AddressOfEntryPoint | `0x6ea1e10` |

Using Unreal Engine 5.6 alone does not imply a matching binary layout. After a game update, confirm the function ABIs, enum structure, and addresses rather than changing only the timestamp or bypassing validation. See [implementation and build maintenance](docs/DEVELOPMENT.md) (Chinese).

## Tests

The default build runs:

- Python tests for configuration validation, build fingerprints, and source packaging.
- An isolated mapping of the local game executable that executes the three original faction conversion functions. It checks original-faction behavior, new-faction round trips, repeated calls, FString ownership, rejection paths, and installation rollback. **It does not execute game startup or connect to a server.**
- A separate process that loads the generated `dwmapi.dll` through its import table, checks forwarded exports, and calls the four DWM APIs used by the game.

To test another executable from the same supported build:

```powershell
.\Build.ps1 -GameExe 'D:\YourGameDirectory\1.exe' `
  -AdditionalTestExe 'D:\YourGameDirectory\kards-Win64-Shipping.exe'
```

To run the tests that do not require game files:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
```

GitHub Actions runs the Python tests, bundled dependency integrity checks, and source packaging only. It does not download the game or validate gameplay.

See the [0.2.0 local validation record](docs/VALIDATION.md) (Chinese) for build and test results.

## Publish on GitHub

Upload the source tree, or generate a clean source archive:

```powershell
python tools/package_source.py
```

The output is `dist/KardsFactionToolkit-source.zip`. Packaging uses an explicit file allowlist and excludes executables, DLLs, game assets, logs, generated machine-code headers, personal paths, and build caches. Extract the archive and commit its contents to your repository. If you publish `dist/dwmapi.dll` as a Release attachment, include `dist/licenses`, the license, and the build manifest as well.

The project does not automatically connect to a remote repository or publish anything. `.gitignore` excludes local build artifacts.

## Source layout

```text
config/                   New-faction configuration
profiles/                 Supported game build addresses and fingerprints
src/core.cpp              Enum extension and faction/deck conversion hooks
src/proxy.cpp             Startup installation, DWM forwarding, status APIs
tools/generate_profile.py Configuration/build validation and C++/MASM generation
tools/package_source.py   Source-only ZIP packaging
tests/                    Python, native conversion, and DWM proxy tests
vendor/minhook/           MinHook 1.3.4 source and original license
docs/                     UE integration, implementation, and maintenance notes
```

Project code is licensed under the [MIT License](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
