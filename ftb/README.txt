Offline ft8 weight and executable repack by OptimaTech Labs. Upstream fx2-cmix-T-ft8 by Jesús Mejuto González builds on Vladimir Ivanov’s fx2-cmix-transformer. GPLv3. See ATTRIBUTION.md for credits.

Python 3.11+ and a C++17 compiler are required. CXX can name a compiler.
python3 pack_ft8.py expand wiki.ftb archive9
python3 pack_ft8.py restore wiki.ftb enwik9
The second command runs the upstream decoder on Linux x86-64 with AVX2. Both commands verify their output SHA-256. To build a package from the official ft8 self-extractor, use python3 pack_ft8.py pack archive9 wiki.ftb.
