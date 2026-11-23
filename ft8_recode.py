"""Package a losslessly recoded ft8 model, rebuild its archive, or restore enwik9."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


MAGIC = b"FT8WT150"
OFFICIAL = "2b3892ca5a43e1b25bcbbbb5bc79f95cc09331953b324333500e61a3b45eba5b"
WIKIPEDIA = "159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(2 << 20), b""):
            value.update(chunk)
    return value.hexdigest()


def copy_n(source, dest, remaining):
    while remaining:
        block = source.read(min(2 << 20, remaining))
        if not block:
            raise ValueError("truncated archive")
        dest.write(block)
        remaining -= len(block)


def build(scratch):
    exe = scratch / "weight15"
    subprocess.run(["c++", "-std=c++17", "-O2", str(Path(__file__).with_name("weight15.cpp")), "-o", exe],
                   check=True)
    return exe


def pack(original, output, scratch, recipe):
    if digest(original) != OFFICIAL:
        raise ValueError("expected the published ft8 archive")
    before = scratch / "old.weights"
    after = scratch / "new.weights"
    with original.open("rb") as src:
        src.seek(-16, os.SEEK_END)
        _, _, payload_size, weights_size = struct.unpack("<4I", src.read(16))
        offset = original.stat().st_size - 16 - payload_size - weights_size
        src.seek(offset)
        with before.open("xb") as f:
            copy_n(src, f, weights_size)
        exe = build(scratch)
        subprocess.run([exe, "pack", before, after, str(recipe)], check=True)
        with output.open("xb") as dest:
            dest.write(MAGIC + struct.pack("<II", offset, after.stat().st_size))
            src.seek(0)
            copy_n(src, dest, offset)
            with after.open("rb") as packed:
                shutil.copyfileobj(packed, dest)
            src.seek(offset + weights_size)
            shutil.copyfileobj(src, dest)


def expand(packed, original, scratch):
    with packed.open("rb") as src, original.open("xb") as dest:
        if src.read(8) != MAGIC:
            raise ValueError("invalid archive header")
        offset, size = struct.unpack("<II", src.read(8))
        if offset + size + 32 > packed.stat().st_size:
            raise ValueError("invalid model boundary")
        copy_n(src, dest, offset)
        weights = scratch / "packed.weights"
        with weights.open("xb") as f:
            copy_n(src, f, size)
        rebuilt = scratch / "original.weights"
        subprocess.run([build(scratch), "unpack", weights, rebuilt], check=True)
        with rebuilt.open("rb") as f:
            shutil.copyfileobj(f, dest)
        shutil.copyfileobj(src, dest)
    if digest(original) != OFFICIAL:
        raise ValueError("rebuilt archive SHA-256 differs from the publisher's")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["pack", "expand", "restore"])
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--recipe", type=int, default=0, choices=range(5))
    args = parser.parse_args()
    source, output = args.input.resolve(), args.output.absolute()
    if output.exists():
        raise FileExistsError(output)
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        scratch = Path(tmp)
        product = scratch / "product"
        if args.action == "pack":
            pack(source, product, scratch, args.recipe)
        else:
            original = scratch / "archive9"
            expand(source, original, scratch)
            if args.action == "expand":
                product = original
            else:
                original.chmod(0o700)
                subprocess.run([original], cwd=scratch, check=True)
                product = scratch / "enwik9_uncompressed"
                if product.stat().st_size != 1_000_000_000 or digest(product) != WIKIPEDIA:
                    raise ValueError("restored Wikipedia failed size or SHA-256 validation")
        os.link(product, output)


if __name__ == "__main__":
    main()
