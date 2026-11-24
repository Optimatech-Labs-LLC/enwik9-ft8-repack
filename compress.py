"""Compress canonical enwik9 with ft8 and repack the result as .ftb or legacy .ftw."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from ft8_recode import WIKIPEDIA, digest, pack


CMIX_SHA256 = "a173a4c5d29b56e29b93554b5938a0efe8ff9528c08845bf4f6e1f0b83cc21ad"
FTW_SHA256 = "1b201d743d110cd0cca8c8ea93d704938456f3e9058947bed8693f03fe6255e7"
FTB_SHA256 = "0fc366fa8c5c6a15d2538cb61437ccba2392ab17899a9c7589f2fac3fae75fb8"


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--cmix", type=Path, required=True, help="ft8 v1.0 Linux encoder")
    cli.add_argument("input", type=Path, help="raw enwik9 XML")
    cli.add_argument("output", type=Path, help="new .ftb archive, or .ftw for v0.1.0")
    args = cli.parse_args()
    target = args.output.absolute()
    if os.path.lexists(target):
        raise FileExistsError(target)
    source = args.input.resolve(strict=True)
    if source.stat().st_size != 1_000_000_000 or digest(source) != WIKIPEDIA:
        raise ValueError("input must be the canonical enwik9 file")

    with tempfile.TemporaryDirectory(dir=target.parent) as tmp:
        stage = Path(tmp)
        executable = stage / "cmix"
        shutil.copyfile(args.cmix, executable)
        if digest(executable) != CMIX_SHA256:
            raise ValueError("encoder SHA-256 differs from ft8 v1.0")
        executable.chmod(0o700)
        (stage / "enwik9").symlink_to(source)
        subprocess.run([executable, "-e", "enwik9", "enwik9.comp"], cwd=stage, check=True)
        if target.suffix == '.ftb':
            result = stage / 'result.ftb'
            codec = Path(__file__).resolve().parent / 'ftb' / 'pack_ft8.py'
            subprocess.run([sys.executable, codec, 'pack', stage / 'archive9', result], check=True)
            expected = FTB_SHA256
        else:
            result = stage / 'result.ftw'
            pack(stage / 'archive9', result, stage, recipe=0)
            expected = FTW_SHA256
        if digest(result) != expected:
            raise ValueError("encoded file differs from the released package")
        os.link(result, target)


if __name__ == "__main__":
    main()
