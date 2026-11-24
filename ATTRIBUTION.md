# Attribution

The benchmark data is enwik9, the first 1,000,000,000 bytes of the March 3, 2006 English Wikipedia XML dump. The text and revision metadata are the work of Wikipedia contributors. The archive restores those bytes as published, including article titles, revision IDs and contributor records. Article histories are available through the page history on English Wikipedia. Mahoney provides the original XML in a freely downloadable ZIP. [Original dataset and download](https://mattmahoney.net/dc/textdata.html) · [English Wikipedia](https://en.wikipedia.org/)

The original Wikipedia text was published under the GNU Free Documentation License. The release includes a copy of version 1.2. Individual quotations and third-party material may carry their original terms. [Corpus license](COPYING-WIKIPEDIA.txt) · [Wikipedia's licensing history](https://en.wikipedia.org/wiki/Wikipedia:Text_of_the_GNU_Free_Documentation_License)

Jesús Mejuto González published fx2-cmix-T-ft8, based on Vladimir Ivanov’s fx2-cmix-transformer. The ft8 encoder, decoder, dictionary, model and article ordering are theirs. Our release includes the upstream source alongside the original encoder. [Upstream release](https://github.com/mejutoco/fx2-cmix-T-ft8/releases/tag/v1.0) · [GPLv3](LICENSE)

OptimaTech Labs changed how the 15-value weight tables and the packed executable are stored. The executable uses the UPX/UCL NRV2B format. Our decoder recovers its original literal and copy choices. [UCL format reader](https://github.com/upx/upx-vendor-ucl/blob/a60611d342b0b7d2924c495ebaa1910e4c3c3fe6/src/n2b_d.c)
