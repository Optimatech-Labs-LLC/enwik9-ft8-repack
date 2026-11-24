# enwik9 ft8 repack

Version 0.2.0 packs Jesús Mejuto González’s 96,945,005-byte ft8 self-extractor into **96,919,084 bytes including the offline decoder**. That saves **25,921 bytes** over the original and **14,873 bytes** over v0.1.0. We rebuilt the complete self-extractor, compared every byte with the official file and matched its SHA-256. Our independent restore of the full 1 GB of Wikipedia is pending. [Download v0.2.0](https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/tag/v0.2.0) · [Upstream ft8](https://github.com/mejutoco/fx2-cmix-T-ft8/releases/tag/v1.0)

| Package | Archive bytes | Decoder ZIP bytes | Total bytes |
|---|---:|---:|---:|
| Official ft8 self-extractor | 96,945,005 | Included | 96,945,005 |
| [v0.1.0](https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/tag/v0.1.0) | 96,916,621 | 17,336 | 96,933,957 |
| **v0.2.0** | **96,896,020** | **23,064** | **96,919,084** |

## How it works

The original model uses 15 possible weight values. Our repacker learns how often those values occur in each matrix and codes their magnitudes using information already decoded in the same rows and columns. The 2,934,692-byte weight block takes 2,902,493 bytes in v0.2.0. We also encode the original literal and copy choices inside the UPX-packed executable. Replaying those choices recovers the exact executable. Jesús’s trained model, compressed Wikipedia text, dictionary and article ordering are reproduced byte for byte. [Weight coder](ftb/weight15.cpp) · [Executable repacker](ftb/nrv.py) · [UPX/UCL format](https://github.com/upx/upx-vendor-ucl/blob/a60611d342b0b7d2924c495ebaa1910e4c3c3fe6/src/n2b_d.c)

## Download and restore

For the full Wikipedia restore, use Linux x86-64 with AVX2, Python 3.11 or later, and a C++17 compiler available as `c++` or through `CXX`. Upstream reports under 10 GB of RAM and about 25 GB of temporary disk. From an empty directory, run the following commands. [Upstream requirements and timings](https://github.com/mejutoco/fx2-cmix-T-ft8#decompress)

```sh
release=https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/download/v0.2.0
curl -fLO "$release/wiki.ftb"
curl -fLO "$release/decoder.zip"
curl -fLO "$release/SHA256SUMS"
sha256sum --check --ignore-missing SHA256SUMS
unzip -q decoder.zip -d decoder
python3 decoder/pack_ft8.py restore wiki.ftb enwik9
```

The decoder first checks the ft8 self-extractor, then checks that the restored Wikipedia file is exactly 1,000,000,000 bytes with SHA-256 `159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc`. All steps after download run offline. Upstream measured 36.1 hours for the full decode on an AMD Ryzen 9 5900X. [Upstream measurement](https://github.com/mejutoco/fx2-cmix-T-ft8#decompress)

## Check the repack in seconds

To verify just the ft8 self-extractor on macOS or Linux, use the same downloaded files. The expected SHA-256 is `2b3892ca5a43e1b25bcbbbb5bc79f95cc09331953b324333500e61a3b45eba5b`. If you have the official file, `cmp archive9 /path/to/official/archive9` compares every byte. [Upstream checksums](https://github.com/mejutoco/fx2-cmix-T-ft8/releases/tag/v1.0) · [Benchmark data](benchmark.json)

```sh
python3 decoder/pack_ft8.py expand wiki.ftb archive9
```

## Reproduce the archive

With a copy of the official ft8 self-extractor, run `python3 decoder/pack_ft8.py pack archive9 rebuilt.ftb`. We checked that this recreates the published `wiki.ftb` byte for byte. Its SHA-256 is `0fc366fa8c5c6a15d2538cb61437ccba2392ab17899a9c7589f2fac3fae75fb8`.

To start from raw enwik9, run the following from a checkout of this repository on Linux x86-64 with AVX2. The `cmix` asset is the original upstream encoder. The script checks the input, encoder and final archive hashes. Upstream reports about 31 hours for the initial compression. [Original enwik9 download](https://mattmahoney.net/dc/textdata.html) · [Upstream compression procedure](https://github.com/mejutoco/fx2-cmix-T-ft8#compress)

```sh
curl -fLO https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/download/v0.2.0/cmix
python3 compress.py --cmix cmix /path/to/enwik9 rebuilt.ftb
```

Jesús Mejuto González’s ft8 builds on Vladimir Ivanov’s fx2-cmix-transformer, fx2-cmix and cmix. Wikipedia contributors supplied the benchmark text. The software is provided under GPLv3. [Software license](LICENSE) · [Attribution and corpus terms](ATTRIBUTION.md)
