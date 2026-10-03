import copy
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace as Obj
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_profile as generator
import package_source


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.document = generator.read_json(ROOT / "config/factions.json")

    def test_default_and_multiple(self):
        self.document["factions"].append({"name": "Hungary", "value": 14, "deck_code": "c"})
        factions = generator.validate_factions(self.document)
        header = generator.faction_header(factions)
        self.assertIn('"EFactionEnum::Hungary", 14', header)
        self.assertIn('L"romania"', header)

    def test_invalid_and_conflicting_factions(self):
        cases = [dict(name="germany"), dict(name='bad"name'), dict(name="中文"),
                 dict(value=12), dict(value=True), dict(deck_code="a"), dict(deck_code="B"),
                 dict(deck_code="bb"), dict(display_name=42)]
        for patch in cases:
            with self.subTest(patch=patch):
                document = copy.deepcopy(self.document)
                document["factions"][0].update(patch)
                with self.assertRaises(ValueError):
                    generator.validate_factions(document)
        for second in ({"name": "rOmAnIa", "value": 14, "deck_code": "c"},
                       {"name": "Hungary", "value": 14, "deck_code": "b"},
                       {"name": "Hungary", "value": 15, "deck_code": "c"}):
            with self.assertRaises(ValueError):
                generator.validate_factions({"schema_version": 1, "factions": self.document["factions"] + [second]})

    def test_all_available_codes(self):
        factions = [{"name": f"Nation{i}", "value": 13+i, "deck_code": chr(ord('b')+i)} for i in range(25)]
        self.assertEqual(len(generator.validate_factions({"schema_version": 1, "factions": factions})), 25)
        for factions in ([], factions + [{"name": "Overflow", "value": 38, "deck_code": "!"}]):
            with self.assertRaises(ValueError):
                generator.validate_factions({"schema_version": 1, "factions": factions})

    def test_profile_validation(self):
        profile = generator.read_json(ROOT / "profiles/kards-20261002-win64.json")
        generator.validate_profile(profile)
        profile["rvas"]["Params"] = profile["image_size"]
        with self.assertRaises(ValueError):
            generator.validate_profile(profile)

    def test_non_object_input(self):
        for value in (None, [], "text", 13):
            with self.assertRaises(ValueError):
                generator.validate_factions(value)
            with self.assertRaises(ValueError):
                generator.validate_profile(value)


class FingerprintTests(unittest.TestCase):
    def setUp(self):
        self.profile = generator.read_json(ROOT / "profiles/kards-20261002-win64.json")
        self.data = {}
        table = bytearray()
        for index, (name, spec) in enumerate(self.profile["functions"].items()):
            rva = generator.number(spec["rva"])
            data = bytes([index + 1]) * 32
            spec["sha256"] = hashlib.sha256(data).hexdigest()
            self.data[rva] = data
            table.extend(struct.pack('<III', rva, rva + len(data), 0))
        self.data[1] = bytes(table)
        self.data[generator.number(self.profile["entry_rva"])] = bytes.fromhex(self.profile["entry_bytes"])
        self.pe = Obj(FILE_HEADER=Obj(Machine=0x8664, TimeDateStamp=generator.number(self.profile["timestamp"])),
                      OPTIONAL_HEADER=Obj(Magic=0x20b, SizeOfImage=generator.number(self.profile["image_size"]),
                          AddressOfEntryPoint=generator.number(self.profile["entry_rva"]),
                          DATA_DIRECTORY=[None, None, None, Obj(VirtualAddress=1, Size=len(table))]),
                      get_data=lambda rva, size: self.data[rva][:size])

    def test_verified_regions(self):
        self.assertEqual(set(generator.validate_game(self.pe, self.profile)), {"Entry", *generator.FUNCTION_NAMES})

    def test_function_tail_change(self):
        rva = generator.number(self.profile["functions"]["Encode"]["rva"])
        self.data[rva] = self.data[rva][:-1] + b'X'
        with self.assertRaisesRegex(ValueError, "Critical function differs"):
            generator.validate_game(self.pe, self.profile)

    def test_version_change(self):
        self.pe.FILE_HEADER.TimeDateStamp += 1
        with self.assertRaisesRegex(ValueError, "Unsupported game build"):
            generator.validate_game(self.pe, self.profile)

    def test_entry_change(self):
        rva = generator.number(self.profile["entry_rva"])
        self.data[rva] = b'\x90' * len(self.data[rva])
        with self.assertRaisesRegex(ValueError, "Startup entry"):
            generator.validate_game(self.pe, self.profile)


class PackageTests(unittest.TestCase):
    def test_archive_omits_local_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("README.md", "src/core.cpp", "src/game.exe", "src/generated/profile.h",
                         "build/generated/profile.h", "dist/dwmapi.dll", "vendor/minhook/LICENSE.txt",
                         ".github/workflows/ci.yml", "tests/__pycache__/test.pyc"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('example', encoding='utf-8')
            output = root / 'dist/source.zip'
            self.assertEqual(package_source.package(root, output), 4)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), {
                    'KardsFactionToolkit/README.md', 'KardsFactionToolkit/src/core.cpp',
                    'KardsFactionToolkit/vendor/minhook/LICENSE.txt', 'KardsFactionToolkit/.github/workflows/ci.yml'})

    def test_forwarding_preserves_ordinals(self):
        sources = generator.forwarding_sources([(None, 17), ('DwmFlush', 20)])
        self.assertIn('Forward0=Forward0 @17 NONAME', sources['dwmapi.def'])
        self.assertIn('DwmFlush=Forward1 @20', sources['dwmapi.def'])
        self.assertIn('movdqu [rsp+70h], xmm3', sources['forwards.asm'])
        with self.assertRaises(ValueError):
            generator.forwarding_sources([('DwmFlush', 20), ('Duplicate', 20)])


if __name__ == '__main__':
    unittest.main()
