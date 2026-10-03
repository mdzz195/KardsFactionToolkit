"""Validate a local game build and generate C++ configuration / DWM forwarding.

No game executable or extracted function bodies are stored in the source tree.
Generated files belong in the ignored build directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

ORIGINAL_NAMES = (
    "NotAvailable", "Germany", "Britain", "Japan", "Soviet", "USA", "France",
    "Italy", "Poland", "Finland", "Anzac", "Allies", "Neutral",
)
FUNCTION_NAMES = ("NameToEnum", "Encode", "Decode", "EnumConstructor")
RVA_NAMES = (
    "Params", "Enumerators", "Outer", "EnumName", "Singleton", "StaticSingleton",
    "TestFree", "TestConstruct",
)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def validate_factions(document):
    if not isinstance(document, dict):
        raise ValueError("Faction configuration must be a JSON object")
    if document.get("schema_version") != 1:
        raise ValueError("Unsupported faction schema_version")
    factions = document.get("factions")
    if not isinstance(factions, list) or not 1 <= len(factions) <= 25:
        raise ValueError("Configure 1..25 new factions (available deck codes: b..z)")
    names = {name.lower() for name in ORIGINAL_NAMES}
    codes = set("0123456789a")
    for index, faction in enumerate(factions):
        if not isinstance(faction, dict):
            raise ValueError("Each faction must be an object")
        name, value, code = (faction.get(key) for key in ("name", "value", "deck_code"))
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,47}", name):
            raise ValueError("Faction name must be a 1..48 character ASCII identifier")
        if name.lower() in names:
            raise ValueError(f"Duplicate or original faction name: {name}")
        if type(value) is not int or value != len(ORIGINAL_NAMES) + index:
            raise ValueError("Faction values must be consecutive, starting at 13, in array order")
        if not isinstance(code, str) or not re.fullmatch(r"[b-z]", code) or code in codes:
            raise ValueError(f"deck_code must be unique and in b..z: {code!r}")
        if "display_name" in faction and not isinstance(faction["display_name"], str):
            raise ValueError("display_name must be a string")
        names.add(name.lower())
        codes.add(code)
    return factions


def number(value):
    if type(value) is int:
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"Expected an integer or hex string: {value!r}")


def validate_profile(profile):
    if not isinstance(profile, dict):
        raise ValueError("Build profile must be a JSON object")
    if profile.get("schema_version") != 1:
        raise ValueError("Unsupported build profile schema_version")
    if not re.fullmatch(r"[a-zA-Z0-9_.-]+", profile.get("id", "")):
        raise ValueError("Invalid profile id")
    size = number(profile["image_size"])
    if not 0 < size <= 0xFFFFFFFF or not 0 <= number(profile["timestamp"]) <= 0xFFFFFFFF:
        raise ValueError("Invalid PE version fields")
    entry = bytes.fromhex(profile["entry_bytes"])
    if not 14 <= len(entry) <= 64:
        raise ValueError("Startup redirect requires 14..64 verified bytes")
    if set(profile["functions"]) != set(FUNCTION_NAMES) or set(profile["rvas"]) != set(RVA_NAMES):
        raise ValueError("Profile function/RVA fields do not match this runtime ABI")
    addresses = [number(profile["entry_rva"])]
    for spec in profile["functions"].values():
        addresses.append(number(spec["rva"]))
        if not re.fullmatch(r"[0-9a-f]{64}", spec["sha256"]):
            raise ValueError("Function SHA256 must be 64 lowercase hex digits")
    addresses.extend(number(value) for value in profile["rvas"].values())
    if any(not 0 < address < size for address in addresses):
        raise ValueError("RVA outside the expected image")
    if number(profile["entry_rva"]) + len(entry) > size:
        raise ValueError("Entry bytes extend outside the image")
    return profile


def validate_game(pe, profile):
    if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.Magic != 0x20B:
        raise ValueError("Game executable must be Windows PE32+ x64")
    actual = (pe.FILE_HEADER.TimeDateStamp, pe.OPTIONAL_HEADER.SizeOfImage,
              pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    expected = tuple(number(profile[key]) for key in ("timestamp", "image_size", "entry_rva"))
    if actual != expected:
        raise ValueError(f"Unsupported game build: PE fields {actual!r}; expected {expected!r}")
    entry = bytes.fromhex(profile["entry_bytes"])
    if pe.get_data(expected[2], len(entry)) != entry:
        raise ValueError("Startup entry bytes differ from the verified profile")
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata = pe.get_data(directory.VirtualAddress, directory.Size)
    if len(pdata) != directory.Size or len(pdata) % 12:
        raise ValueError("Invalid x64 exception directory")
    functions = {start: end for start, end, _ in struct.iter_unpack("<III", pdata)}
    regions = {"Entry": (expected[2], entry)}
    for name in FUNCTION_NAMES:
        spec = profile["functions"][name]
        rva = number(spec["rva"])
        end = functions.get(rva, 0)
        if not rva < end <= expected[1]:
            raise ValueError(f"Missing/invalid function boundary: {name}")
        data = pe.get_data(rva, end - rva)
        if len(data) != end - rva or hashlib.sha256(data).hexdigest() != spec["sha256"]:
            raise ValueError(f"Critical function differs: {name}; analyze this game version first")
        regions[name] = (rva, data)
    return regions


def faction_header(factions):
    lines = ["#pragma once", "#include <cstddef>", "#include <cstdint>", "namespace Config {",
             "inline constexpr char Version[] = \"0.2.0\";",
             f"inline constexpr size_t OriginalCount = {len(ORIGINAL_NAMES)};",
             "struct FactionSpec { const wchar_t* name; const wchar_t* lowerName;",
             "    const char* enumName; uint8_t value; wchar_t code; };",
             "inline constexpr FactionSpec Factions[] = {"]
    for faction in factions:
        name, value, code = (faction[key] for key in ("name", "value", "deck_code"))
        lines.append(f'    {{L"{name}", L"{name.lower()}", "EFactionEnum::{name}", {value}, L\'{code}\'}},')
    lines += ["};", "inline constexpr size_t FactionCount = sizeof(Factions)/sizeof(Factions[0]);", "}"]
    return "\n".join(lines) + "\n"


def forwarding_sources(exports):
    defs = ["LIBRARY dwmapi", "EXPORTS", "  KardsFactionStatus", "  KardsFactionVersion",
            "  RomaniaNativeStatus", "  RomaniaNativeVersion"]
    asm = ["option casemap:none", "EXTERN ResolveDwmExport:PROC", ".code"]
    spec = ["#pragma once", "struct DwmExport { const char* name; unsigned short ordinal; };",
            "inline constexpr DwmExport DwmExports[] = {"]
    seen = set()
    for index, (name, ordinal) in enumerate(exports):
        if not 1 <= ordinal <= 65535 or ordinal in seen:
            raise ValueError("Invalid or duplicate DWM export ordinal")
        if name is not None and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"Unsupported DWM export name: {name!r}")
        seen.add(ordinal)
        symbol = f"Forward{index}"
        defs.append(f"  {name or symbol}={symbol} @{ordinal}" + ("" if name else " NONAME") +
                    (" PRIVATE" if name in ("DllCanUnloadNow", "DllGetClassObject") else ""))
        spec.append("    {" + (f'"{name}"' if name else "nullptr") + f", {ordinal}" + "},")
        asm += [f"{symbol} PROC FRAME", "  sub rsp, 0A8h", "  .allocstack 0A8h", "  .endprolog",
                "  mov [rsp+20h], rcx", "  mov [rsp+28h], rdx", "  mov [rsp+30h], r8", "  mov [rsp+38h], r9",
                "  movdqu [rsp+40h], xmm0", "  movdqu [rsp+50h], xmm1",
                "  movdqu [rsp+60h], xmm2", "  movdqu [rsp+70h], xmm3",
                f"  mov ecx, {index}", "  call ResolveDwmExport", "  mov r11, rax",
                "  mov rcx, [rsp+20h]", "  mov rdx, [rsp+28h]", "  mov r8, [rsp+30h]", "  mov r9, [rsp+38h]",
                "  movdqu xmm0, [rsp+40h]", "  movdqu xmm1, [rsp+50h]",
                "  movdqu xmm2, [rsp+60h]", "  movdqu xmm3, [rsp+70h]",
                "  add rsp, 0A8h", "  jmp r11", f"{symbol} ENDP"]
    return {"dwmapi.def": "\n".join(defs) + "\n", "forwards.asm": "\n".join(asm + ["END"]) + "\n",
            "dwm_exports.h": "\n".join(spec + ["};"]) + "\n"}


def generate(args):
    import pefile
    factions = validate_factions(read_json(args.config))
    profile = validate_profile(read_json(args.profile))
    game = Path(args.game_exe).resolve(strict=True)
    raw = game.read_bytes()
    with pefile.PE(data=raw, fast_load=True) as pe:
        regions = validate_game(pe, profile)
    system = Path(args.system_dwm or Path(os.environ["SystemRoot"]) / "System32/dwmapi.dll")
    with pefile.PE(str(system), fast_load=True) as dwm:
        if dwm.FILE_HEADER.Machine != 0x8664:
            raise ValueError("Use 64-bit Python and the 64-bit System32/dwmapi.dll")
        dwm.parse_data_directories(directories=[0])
        exports = [(symbol.name.decode("ascii") if symbol.name else None, symbol.ordinal)
                   for symbol in sorted(dwm.DIRECTORY_ENTRY_EXPORT.symbols, key=lambda symbol: symbol.ordinal)]
    required = {"DwmIsCompositionEnabled", "DwmFlush", "DwmGetCompositionTimingInfo", "DwmSetWindowAttribute"}
    if not required.issubset({name for name, _ in exports}):
        raise ValueError("System DWM is missing required game exports")
    lines = ["#pragma once", "#include <cstdint>", "#include <cstddef>", "namespace Profile {",
             f'inline constexpr char Id[] = "{profile["id"]}";',
             f'inline constexpr uint32_t Timestamp = 0x{number(profile["timestamp"]):x};',
             f'inline constexpr uint32_t ImageSize = 0x{number(profile["image_size"]):x};']
    for name, (rva, data) in regions.items():
        lines += [f"inline constexpr uint32_t {name}Rva = 0x{rva:x};",
                  f"inline constexpr unsigned char {name}Bytes[] = {{" + ",".join(f"0x{byte:02x}" for byte in data) + "};"]
    for name, value in profile["rvas"].items():
        lines.append(f"inline constexpr uint32_t {name}Rva = 0x{number(value):x};")
    outputs = forwarding_sources(exports)
    outputs["profile.h"] = "\n".join(lines + ["}"]) + "\n"
    outputs["factions.h"] = faction_header(factions)
    # This local manifest deliberately omits absolute paths.
    outputs["manifest.json"] = json.dumps({
        "tool_version": "0.2.0", "profile": profile["id"], "game_file": game.name,
        "source_sha256_evidence_only": hashlib.sha256(raw).hexdigest(),
        "validation": "PE version fields, critical functions, original enum schema; no whole-file hash gate",
        "factions": factions, "forwarded_exports": len(exports),
        "system_dwm_sha256": hashlib.sha256(system.read_bytes()).hexdigest(),
    }, indent=2, ensure_ascii=False) + "\n"
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in outputs.items():
        temporary = out / (name + ".tmp")
        temporary.write_text(text, encoding="utf-8", newline="\n")
        temporary.replace(out / name)
    print(f"Validated {profile['id']}; {len(factions)} new faction(s); {len(exports)} DWM exports")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--system-dwm", help="Defaults to Windows System32/dwmapi.dll")
    args = parser.parse_args()
    try:
        generate(args)
    except (OSError, ValueError, KeyError, TypeError, ImportError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
