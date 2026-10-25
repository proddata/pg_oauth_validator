#!/usr/bin/env python3
"""Copy the working source tree without Git metadata or ignored local files."""

import argparse
import pathlib
import shutil
import subprocess


def copy_source(source, destination):
    source = source.resolve()
    # The read-only bind mount may have a different owner from the container.
    # Trust only this explicit checkout, without changing global Git settings.
    result = subprocess.run(
        [
            "git", "-c", f"safe.directory={source}", "-C", str(source),
            "ls-files", "--cached", "--others", "--exclude-standard", "-z",
        ],
        check=True,
        stdout=subprocess.PIPE,
    )
    # NUL separation preserves spaces and newlines in developer file names.
    for name in sorted(set(result.stdout.split(b"\0")) - {b""}):
        relative = pathlib.Path(name.decode("utf-8", errors="surrogateescape"))
        original = source / relative
        # A tracked file deleted in the working tree must stay deleted here.
        if not original.exists() and not original.is_symlink():
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target, follow_symlinks=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    args = parser.parse_args()
    copy_source(args.source, args.destination)
