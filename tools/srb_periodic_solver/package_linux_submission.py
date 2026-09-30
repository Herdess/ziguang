#!/usr/bin/env python3
"""Build a Zip64 Linux submission while preserving Unix executable permissions."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import time
import zipfile
from pathlib import Path


def add_file(archive: zipfile.ZipFile, source: Path, name: str, mode: int) -> None:
    timestamp = time.localtime(source.stat().st_mtime)[:6]
    info = zipfile.ZipInfo(name, timestamp)
    info.create_system = 3
    info.external_attr = mode << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    with source.open("rb") as input_stream, archive.open(info, "w", force_zip64=True) as output_stream:
        shutil.copyfileobj(input_stream, output_stream, length=8 * 1024 * 1024)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--estimate", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for path in (args.estimate, args.model):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    directory = zipfile.ZipInfo("bin/")
    directory.create_system = 3
    directory.external_attr = (0o040755 << 16) | 0x10
    with zipfile.ZipFile(
        args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True
    ) as archive:
        archive.writestr(directory, b"")
        add_file(archive, args.estimate, "bin/estimate", 0o100755)
        add_file(archive, args.model, "bin/grid208_4x8_u16.bin", 0o100644)

    with zipfile.ZipFile(args.output) as archive:
        expected = ["bin/", "bin/estimate", "bin/grid208_4x8_u16.bin"]
        if archive.namelist() != expected:
            raise RuntimeError(f"unexpected archive entries: {archive.namelist()}")
        if archive.testzip() is not None:
            raise RuntimeError("archive CRC verification failed")
        executable_mode = archive.getinfo("bin/estimate").external_attr >> 16
        if executable_mode & 0o111 == 0:
            raise RuntimeError("bin/estimate is not marked executable")
    print(f"output={args.output}")
    print(f"bytes={args.output.stat().st_size}")
    print(f"sha256={sha256(args.output)}")


if __name__ == "__main__":
    main()
