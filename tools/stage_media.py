#!/usr/bin/env python3
"""Stage untouched Amiga media into a lower-case desktop data tree."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


# media/levels/level_a is a compact development fixture in the retained source tree.
# The maintained runtime still opens AB3:levels/level_a, while the playable
# authored A bundle is retained verbatim under demolevels/level_a. Overlay
# only the staged alias;
# neither source asset is changed.
AUTHORED_LEVEL_A_SOURCE = Path("demolevels") / "level_a"
RUNTIME_LEVEL_A_DESTINATION = Path("levels") / "level_a"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    destination = args.destination.resolve()

    if not source.is_dir():
        print(f"error: source media directory does not exist: {source}", file=sys.stderr)
        return 1
    if source == destination or source in destination.parents or destination in source.parents:
        print("error: source and destination media trees must not overlap", file=sys.stderr)
        return 1

    staged_paths: set[Path] = set()
    for source_file in source.rglob("*"):
        if not source_file.is_file():
            continue
        relative = source_file.relative_to(source)
        staged_relative = Path(*(part.lower() for part in relative.parts))
        if staged_relative in staged_paths:
            print(
                f"error: case-folding collision while staging media: {staged_relative}",
                file=sys.stderr,
            )
            return 1
        staged_paths.add(staged_relative)

    if destination.exists():
        shutil.rmtree(destination)
    for source_file in source.rglob("*"):
        if not source_file.is_file():
            continue
        relative = source_file.relative_to(source)
        target = destination.joinpath(*(part.lower() for part in relative.parts))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, target)

    authored_level_a = source / AUTHORED_LEVEL_A_SOURCE
    if not authored_level_a.is_dir():
        print(
            f"error: playable authored Level A bundle is missing: {authored_level_a}",
            file=sys.stderr,
        )
        return 1
    runtime_level_a = destination / RUNTIME_LEVEL_A_DESTINATION
    for source_file in authored_level_a.iterdir():
        if source_file.is_file():
            runtime_level_a.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, runtime_level_a / source_file.name.lower())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
