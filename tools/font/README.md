# Hiveton font package tool

This tool converts one `.ttf`/`.otf` into one device-friendly `.hdfont` package.
Users only need to copy this single output file to the TF card, for example `/font/zk.hdfont`.

The package can contain multiple font sizes, so firmware can choose the matching size without asking the user to copy several files.
By default it includes the reading sizes: `18,20,22,24,26,28,30`.

## Build

```sh
go build -o fonttool .
```

## Examples

Build one package for one book plus punctuation:

```sh
./fonttool \
  -font /Volumes/TF/font/zk.ttf \
  -text /Volumes/TF/books/09_银线.txt \
  -out /Volumes/TF/font/zk.hdfont
```

Build one generic CJK package:

```sh
./fonttool -font ./zk.ttf -preset cjk -out ./zk.hdfont
```

Build one package with explicit sizes:

```sh
./fonttool -font ./zk.ttf -sizes 20,22,24,26 -out ./zk.hdfont
```

Build a single-size package:

```sh
./fonttool -font ./zk.ttf -sizes "" -size 22 -out ./zk_22.hdfont
```

Add custom Unicode ranges:

```sh
./fonttool -font ./zk.ttf -ranges U+4E00-U+9FFF,U+3000-U+303F -out ./zk.hdfont
```

## Package format

All integer fields are little-endian.

Package header is 64 bytes:

```text
0   8   magic: "HDFPKG1\0"
8   2   version: 1
10  2   flags: bit0=RLE, bit1=alpha8
12  4   face_count
16  4   directory_offset, currently 64
20  4   directory_entry_size, currently 16
24  4   face_data_offset
28  36  reserved
```

Each package directory entry is 16 bytes:

```text
0   2   size_px
2   2   reserved
4   4   face_offset from file start
8   4   face_length
12  4   glyph_count
```

Each face block starts with an embedded `HDFNTC1\0` face header.

Face header is 64 bytes:

```text
0   8   magic: "HDFNTC1\0"
8   2   version: 1
10  2   flags: bit0=RLE, bit1=alpha8
12  2   size_px
14  2   reserved
16  4   glyph_count
20  4   line_height_26_6
24  4   ascent_26_6
28  4   descent_26_6
32  4   glyph_index_offset
36  4   bitmap_offset
40  4   bitmap_size
44  20  reserved
```

Each glyph index entry is 24 bytes:

```text
0   4   Unicode codepoint
4   4   bitmap offset relative to bitmap section
8   4   bitmap encoded length
12  4   advance in 26.6 fixed-point pixels
16  2   x offset in pixels
18  2   y offset in pixels relative to baseline
20  2   bitmap width
22  2   bitmap height
```

RLE encoding:

```text
00..7F: zero run, length = token + 1
80..FF: literal run, length = (token & 0x7F) + 1, followed by literal alpha bytes
```
