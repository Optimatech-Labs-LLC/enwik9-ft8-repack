# enwik9 ft8 weight repack

OptimaTech Labs re-encoded the prediction weights in Jesús Mejuto González’s fx2-cmix-T-ft8 archive. The result occupies **96,933,957 bytes including the offline decoder**, saving **11,048 bytes** against the official self-extracting file. The package reconstructs all 96,945,005 bytes of the official archive. Our own full restore through to the 1 GB Wikipedia file is pending. [Download v0.1.0](https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/tag/v0.1.0) · [Upstream ft8](https://github.com/mejutoco/fx2-cmix-T-ft8)

| Component | Bytes |
|---|---:|
| `wiki.ftw` | 96,916,621 |
| `decoder.zip` | 17,336 |
| **Total** | **96,933,957** |

## What changed

The upstream model contains 5,868,864 quantized weights drawn from 15 values. We encode them with per-tensor statistics and omit the unused sixteenth symbol. The weights shrink from 2,934,692 to 2,906,292 bytes. A 16-byte header locates the replacement block. Decoding reconstructs the exact original weight representation, including its original coding. The text bitstream, dictionary, model, and internal article reordering are ft8’s. Jesús Mejuto González’s ft8 release builds on Vladimir Ivanov’s fx2-cmix-transformer, fx2-cmix and cmix. [Our weight coder](weight15.cpp) · [Upstream source at the tested revision](https://github.com/mejutoco/fx2-cmix-T-ft8/tree/6799899710f83c6d9c62fdb742ddbf489443671f)

## Download and restore

The full restore uses Linux x86-64 with AVX2, Python 3.8 or later and a C++17 compiler accessible as `c++`. Upstream reports under 10 GB of RAM and about 25 GB of temporary disk. From an empty directory, download the two scored files and their checksum manifest. [Upstream hardware and timing](https://github.com/mejutoco/fx2-cmix-T-ft8#decompress)

```sh
release=https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/download/v0.1.0
curl -fLO "$release/wiki.ftw"
curl -fLO "$release/decoder.zip"
curl -fLO "$release/SHA256SUMS"
sha256sum --check --ignore-missing SHA256SUMS
unzip -q decoder.zip -d decoder
python3 decoder/ft8_recode.py restore wiki.ftw enwik9
sha256sum enwik9
```

The expected enwik9 SHA-256 is `159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc`. The decoder checks the reconstructed self-extractor before running it, then checks the output size and hash. All steps after download run offline.

## Verify the repack in seconds

The shorter check ends at the official self-extracting archive. We ran the scored decoder, compared the whole result to an independent copy of the official `archive9`, and matched the publisher’s SHA-256. Expansion took 0.765 seconds on a DGX Spark, including the C++ compilation. The later x86 Wikipedia decoding stage runs separately. [Upstream checksums](https://github.com/mejutoco/fx2-cmix-T-ft8/releases/tag/v1.0) · [Measurement](benchmark.json)

```sh
python3 decoder/ft8_recode.py expand wiki.ftw rebuilt-archive9
echo '2b3892ca5a43e1b25bcbbbb5bc79f95cc09331953b324333500e61a3b45eba5b  rebuilt-archive9' | sha256sum --check
```

The publisher measured a 36.1-hour full decode on a Ryzen 9 5900X. Our partial Intel Xeon Gold 6548N run reached 0.45% and matched 2,686,976 intermediate bytes. We have no completed full-file decode timing on that machine.

## Reproduce the archive from raw enwik9

Run the following from a checkout of this repository. The separate `cmix` asset is the unmodified upstream v1.0 compressor. The script checks the input and encoder hashes, runs ft8 in an isolated directory, and applies weight recipe 0. It checks the final archive hash before installing the output file. The release size measurement used the published upstream archive as input to the repacker. Upstream reports 31 hours for its raw enwik9 compression step. [Corpus](https://mattmahoney.net/dc/textdata.html) · [Upstream compression procedure](https://github.com/mejutoco/fx2-cmix-T-ft8#compress)

```sh
curl -fLO https://github.com/Optimatech-Labs-LLC/enwik9-ft8-repack/releases/download/v0.1.0/cmix
python3 compress.py --cmix cmix /path/to/enwik9 rebuilt.ftw
```

Given the official `archive9`, the weight step can also be run directly with `python3 ft8_recode.py pack archive9 rebuilt.ftw --recipe 0`.

The programs are provided under GPLv3. The release also provides the corresponding upstream source archive. The Wikipedia corpus carries its own terms. [Software license](LICENSE) · [Attribution and corpus terms](ATTRIBUTION.md)
