"""Create a source-only GitHub upload archive using a fixed file allowlist."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOP_FILES = {"README.md", "README.en.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md",
             "CMakeLists.txt", "Build.ps1", "requirements.txt", ".gitignore", ".gitattributes"}
SOURCE_DIRS = {"src", "tests", "tools", "vendor", "config", "profiles", "docs", ".github"}
EXTENSIONS = {".cpp", ".c", ".h", ".py", ".ps1", ".json", ".md", ".txt", ".yml", ".yaml"}


def source_files(root):
    result = []
    for path in root.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(root)
        if relative.as_posix() in TOP_FILES or (
            relative.parts[0] in SOURCE_DIRS and path.suffix in EXTENSIONS
            and not {"__pycache__", "generated", "build", "backups"}.intersection(relative.parts)
        ):
            result.append(path)
    return sorted(result)


def package(root, output):
    files = source_files(root)
    if not files or not (root / "README.md").is_file():
        raise ValueError("Source tree or README is missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, "KardsFactionToolkit/" + path.relative_to(root).as_posix())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip():
            raise ValueError("Archive integrity check failed")
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/KardsFactionToolkit-source.zip")
    args = parser.parse_args()
    count = package(ROOT, args.output)
    print(f"Packaged {count} source files: {args.output.resolve()}")


if __name__ == "__main__":
    main()
