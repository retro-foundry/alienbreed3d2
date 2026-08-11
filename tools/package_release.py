#!/usr/bin/env python3
"""Package the staged Windows runtime output created by the CMake build."""

import argparse
from pathlib import Path
import re
import shutil
import sys
import zipfile


ROOT = Path(__file__).resolve().parent.parent
VERSION_PATTERN = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._-]*$")


def require_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"required release file is missing: {path}")


def require_dir(path: Path) -> None:
    if not path.is_dir():
        raise FileNotFoundError(f"required release directory is missing: {path}")


def zip_tree(source: Path, archive_path: Path) -> None:
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, Path(source.name) / path.relative_to(source))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a Windows x64 release ZIP.")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not VERSION_PATTERN.fullmatch(args.version):
        print("Version may contain only letters, digits, '.', '_', and '-'.", file=sys.stderr)
        return 1

    runtime_dir = (args.build_dir / args.config).resolve()
    output_dir = args.output_dir.resolve()
    package_name = f"alien-breed-3d-ii-{args.version}-windows-x64"
    stage_dir = output_dir / "stage" / package_name
    archive_path = output_dir / f"{package_name}.zip"

    try:
        require_dir(runtime_dir)
        require_file(runtime_dir / "ab3d2.exe")
        require_file(runtime_dir / "ab3d2.ini")
        require_file(runtime_dir / "README.txt")
        require_dir(runtime_dir / "data")

        if stage_dir.exists():
            shutil.rmtree(stage_dir)
        stage_dir.mkdir(parents=True)
        shutil.copy2(runtime_dir / "ab3d2.exe", stage_dir / "ab3d2.exe")
        shutil.copy2(runtime_dir / "ab3d2.ini", stage_dir / "ab3d2.ini")
        shutil.copy2(runtime_dir / "README.txt", stage_dir / "README.txt")
        shutil.copytree(runtime_dir / "data", stage_dir / "data")

        output_dir.mkdir(parents=True, exist_ok=True)
        if archive_path.exists():
            archive_path.unlink()
        zip_tree(stage_dir, archive_path)
        shutil.rmtree(output_dir / "stage")
    except (OSError, shutil.Error) as exc:
        print(f"Could not create Windows release: {exc}", file=sys.stderr)
        return 1

    print(archive_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
