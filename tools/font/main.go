package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"image/color"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/math/fixed"
)

const (
	packageMagic        = "HDFPKG1\x00"
	faceMagic           = "HDFNTC1\x00"
	version             = uint16(1)
	flagRLE             = uint16(1 << 0)
	flagAlpha8          = uint16(1 << 1)
	packageHeaderSize   = 64
	packageDirEntrySize = 16
	faceHeaderSize      = 64
	glyphEntrySize      = 24
	defaultDPI          = 72
	defaultOut          = "font.hdfont"
	defaultSizes        = "18,20,22,24,26,28,30"
)

type stringList []string

func (s *stringList) String() string {
	return strings.Join(*s, ",")
}

func (s *stringList) Set(v string) error {
	if v == "" {
		return nil
	}
	*s = append(*s, v)
	return nil
}

type glyphEntry struct {
	Rune      rune
	Offset    uint32
	Length    uint32
	Advance26 int32
	XOff      int16
	YOff      int16
	Width     uint16
	Height    uint16
}

type faceBlob struct {
	SizePx     int
	GlyphCount int
	Missing    int
	BitmapSize int
	Data       []byte
}

func main() {
	var textPaths stringList
	fontPath := flag.String("font", "", "input .ttf/.otf font path")
	outPath := flag.String("out", defaultOut, "output single .hdfont package path")
	sizesSpec := flag.String("sizes", defaultSizes, "comma-separated font sizes included in the single output file")
	sizePx := flag.Int("size", 22, "fallback single font size when -sizes is empty")
	preset := flag.String("preset", "book", "charset preset: book, minimal, cjk")
	ranges := flag.String("ranges", "", "extra unicode ranges, e.g. U+4E00-U+9FFF,U+3000-U+303F")
	extraChars := flag.String("chars", "", "extra literal characters to include")
	noRLE := flag.Bool("raw", false, "store raw alpha8 bitmaps instead of RLE")
	flag.Var(&textPaths, "text", "text file or directory used to collect glyphs; repeatable")
	flag.Parse()

	if *fontPath == "" {
		fatal("missing -font")
	}

	sizes, err := parseSizes(*sizesSpec, *sizePx)
	if err != nil {
		fatal(err.Error())
	}

	charset, err := buildCharset(*preset, *ranges, *extraChars, textPaths)
	if err != nil {
		fatal(err.Error())
	}
	if len(charset) == 0 {
		fatal("empty charset")
	}

	if err := buildFontPackage(*fontPath, *outPath, sizes, charset, !*noRLE); err != nil {
		fatal(err.Error())
	}
}

func fatal(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "fonttool: "+format+"\n", args...)
	os.Exit(1)
}

func parseSizes(spec string, fallback int) ([]int, error) {
	if strings.TrimSpace(spec) == "" {
		if fallback <= 0 || fallback > 255 {
			return nil, errors.New("-size must be 1..255")
		}
		return []int{fallback}, nil
	}

	seen := map[int]struct{}{}
	var sizes []int
	for _, part := range strings.Split(spec, ",") {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		v, err := strconv.Atoi(part)
		if err != nil || v <= 0 || v > 255 {
			return nil, fmt.Errorf("invalid font size %q", part)
		}
		if _, ok := seen[v]; ok {
			continue
		}
		seen[v] = struct{}{}
		sizes = append(sizes, v)
	}
	if len(sizes) == 0 {
		return nil, errors.New("empty -sizes")
	}
	sort.Ints(sizes)
	return sizes, nil
}

func buildCharset(preset, ranges, chars string, textPaths []string) ([]rune, error) {
	set := map[rune]struct{}{}

	addRange := func(lo, hi rune) {
		for r := lo; r <= hi; r++ {
			if keepRune(r) {
				set[r] = struct{}{}
			}
		}
	}

	switch strings.ToLower(strings.TrimSpace(preset)) {
	case "", "book":
		addRange(0x20, 0x7e)
		addRange(0xa0, 0xff)
		addRange(0x2000, 0x206f)
		addRange(0x3000, 0x303f)
		addRange(0xff00, 0xffef)
	case "minimal":
		addRange(0x20, 0x7e)
		addRange(0x3000, 0x303f)
		addRange(0xff00, 0xffef)
	case "cjk":
		addRange(0x20, 0x7e)
		addRange(0xa0, 0xff)
		addRange(0x2000, 0x206f)
		addRange(0x3000, 0x303f)
		addRange(0x3400, 0x4dbf)
		addRange(0x4e00, 0x9fff)
		addRange(0xf900, 0xfaff)
		addRange(0xff00, 0xffef)
	default:
		return nil, fmt.Errorf("unknown -preset %q", preset)
	}

	for _, r := range chars {
		if keepRune(r) {
			set[r] = struct{}{}
		}
	}

	for _, path := range textPaths {
		if err := collectTextRunes(path, set); err != nil {
			return nil, err
		}
	}

	if ranges != "" {
		if err := parseRanges(ranges, set); err != nil {
			return nil, err
		}
	}

	out := make([]rune, 0, len(set))
	for r := range set {
		out = append(out, r)
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out, nil
}

func keepRune(r rune) bool {
	if r == '\uFFFD' || r == '\n' || r == '\r' || r == '\t' {
		return false
	}
	return r >= 0x20
}

func collectTextRunes(path string, set map[rune]struct{}) error {
	info, err := os.Stat(path)
	if err != nil {
		return err
	}
	if !info.IsDir() {
		return collectTextFileRunes(path, set)
	}
	return filepath.WalkDir(path, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		return collectTextFileRunes(p, set)
	})
}

func collectTextFileRunes(path string, set map[rune]struct{}) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	for _, r := range string(data) {
		if keepRune(r) {
			set[r] = struct{}{}
		}
	}
	return nil
}

func parseRanges(spec string, set map[rune]struct{}) error {
	for _, part := range strings.Split(spec, ",") {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		loText, hiText, ok := strings.Cut(part, "-")
		if !ok {
			hiText = loText
		}
		lo, err := parseCodepoint(loText)
		if err != nil {
			return err
		}
		hi, err := parseCodepoint(hiText)
		if err != nil {
			return err
		}
		if hi < lo {
			return fmt.Errorf("invalid range %q", part)
		}
		for r := lo; r <= hi; r++ {
			if keepRune(r) {
				set[r] = struct{}{}
			}
		}
	}
	return nil
}

func parseCodepoint(s string) (rune, error) {
	s = strings.TrimSpace(strings.TrimPrefix(strings.ToUpper(s), "U+"))
	v, err := strconv.ParseInt(s, 16, 32)
	if err != nil {
		return 0, fmt.Errorf("invalid codepoint %q", s)
	}
	return rune(v), nil
}

func buildFontPackage(fontPath, outPath string, sizes []int, charset []rune, useRLE bool) error {
	fontBytes, err := os.ReadFile(fontPath)
	if err != nil {
		return err
	}
	parsed, err := opentype.Parse(fontBytes)
	if err != nil {
		return err
	}

	faces := make([]faceBlob, 0, len(sizes))
	for _, size := range sizes {
		face, err := buildFaceBlob(parsed, size, charset, useRLE)
		if err != nil {
			return err
		}
		faces = append(faces, face)
		fmt.Printf("fonttool: face size=%dpx glyphs=%d missing=%d bitmap=%d bytes face=%d bytes rle=%v\n",
			face.SizePx, face.GlyphCount, face.Missing, face.BitmapSize, len(face.Data), useRLE)
	}

	flags := flagAlpha8
	if useRLE {
		flags |= flagRLE
	}

	out := bytes.Buffer{}
	header := make([]byte, packageHeaderSize)
	copy(header[0:8], []byte(packageMagic))
	put16(header, 8, version)
	put16(header, 10, flags)
	put32(header, 12, uint32(len(faces)))
	put32(header, 16, uint32(packageHeaderSize))
	put32(header, 20, uint32(packageDirEntrySize))
	put32(header, 24, uint32(packageHeaderSize+len(faces)*packageDirEntrySize))
	out.Write(header)

	offset := packageHeaderSize + len(faces)*packageDirEntrySize
	for _, face := range faces {
		entry := make([]byte, packageDirEntrySize)
		put16(entry, 0, uint16(face.SizePx))
		put16(entry, 2, 0)
		put32(entry, 4, uint32(offset))
		put32(entry, 8, uint32(len(face.Data)))
		put32(entry, 12, uint32(face.GlyphCount))
		out.Write(entry)
		offset += len(face.Data)
	}

	for _, face := range faces {
		out.Write(face.Data)
	}

	if err := ensureParentDir(outPath); err != nil {
		return err
	}
	if err := os.WriteFile(outPath, out.Bytes(), 0o644); err != nil {
		return err
	}

	fmt.Printf("fonttool: wrote single font package %s\n", outPath)
	fmt.Printf("fonttool: sizes=%v glyphs=%d total=%d bytes\n", sizes, len(charset), out.Len())
	return nil
}

func buildFaceBlob(parsed *opentype.Font, sizePx int, charset []rune, useRLE bool) (faceBlob, error) {
	face, err := opentype.NewFace(parsed, &opentype.FaceOptions{
		Size:    float64(sizePx),
		DPI:     defaultDPI,
		Hinting: font.HintingFull,
	})
	if err != nil {
		return faceBlob{}, err
	}
	defer face.Close()

	metrics := face.Metrics()
	entries := make([]glyphEntry, 0, len(charset))
	bitmapData := bytes.Buffer{}
	missing := 0

	for _, r := range charset {
		entry, bitmap, ok := rasterizeGlyph(face, r, useRLE)
		if !ok {
			missing++
			continue
		}
		entry.Offset = uint32(bitmapData.Len())
		entry.Length = uint32(len(bitmap))
		entries = append(entries, entry)
		bitmapData.Write(bitmap)
	}

	if len(entries) == 0 {
		return faceBlob{}, errors.New("font produced zero glyphs")
	}

	flags := flagAlpha8
	if useRLE {
		flags |= flagRLE
	}

	indexOffset := uint32(faceHeaderSize)
	bitmapOffset := uint32(faceHeaderSize + len(entries)*glyphEntrySize)
	header := make([]byte, faceHeaderSize)
	copy(header[0:8], []byte(faceMagic))
	put16(header, 8, version)
	put16(header, 10, flags)
	put16(header, 12, uint16(sizePx))
	put16(header, 14, 0)
	put32(header, 16, uint32(len(entries)))
	put32(header, 20, uint32(metrics.Height))
	put32(header, 24, uint32(metrics.Ascent))
	put32(header, 28, uint32(metrics.Descent))
	put32(header, 32, indexOffset)
	put32(header, 36, bitmapOffset)
	put32(header, 40, uint32(bitmapData.Len()))

	out := bytes.Buffer{}
	out.Write(header)
	for _, e := range entries {
		binary.Write(&out, binary.LittleEndian, uint32(e.Rune))
		binary.Write(&out, binary.LittleEndian, e.Offset)
		binary.Write(&out, binary.LittleEndian, e.Length)
		binary.Write(&out, binary.LittleEndian, e.Advance26)
		binary.Write(&out, binary.LittleEndian, e.XOff)
		binary.Write(&out, binary.LittleEndian, e.YOff)
		binary.Write(&out, binary.LittleEndian, e.Width)
		binary.Write(&out, binary.LittleEndian, e.Height)
	}
	out.Write(bitmapData.Bytes())

	return faceBlob{
		SizePx:     sizePx,
		GlyphCount: len(entries),
		Missing:    missing,
		BitmapSize: bitmapData.Len(),
		Data:       out.Bytes(),
	}, nil
}

func rasterizeGlyph(face font.Face, r rune, useRLE bool) (glyphEntry, []byte, bool) {
	dr, mask, maskp, advance, ok := face.Glyph(fixed.P(0, 0), r)
	if !ok {
		return glyphEntry{}, nil, false
	}

	w := dr.Dx()
	h := dr.Dy()
	if w < 0 || h < 0 || w > 65535 || h > 65535 {
		return glyphEntry{}, nil, false
	}

	entry := glyphEntry{
		Rune:      r,
		Advance26: int32(advance),
		XOff:      int16(dr.Min.X),
		YOff:      int16(dr.Min.Y),
		Width:     uint16(w),
		Height:    uint16(h),
	}

	if w == 0 || h == 0 || mask == nil {
		return entry, nil, true
	}

	raw := make([]byte, w*h)
	idx := 0
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			mx := maskp.X + x
			my := maskp.Y + y
			a := color.AlphaModel.Convert(mask.At(mx, my)).(color.Alpha).A
			raw[idx] = a
			idx++
		}
	}

	if useRLE {
		return entry, encodeRLE(raw), true
	}
	return entry, raw, true
}

func encodeRLE(src []byte) []byte {
	out := make([]byte, 0, len(src)/2)
	for i := 0; i < len(src); {
		if src[i] == 0 {
			count := 1
			for i+count < len(src) && src[i+count] == 0 && count < 128 {
				count++
			}
			out = append(out, byte(count-1))
			i += count
			continue
		}

		start := i
		count := 1
		for start+count < len(src) && src[start+count] != 0 && count < 128 {
			count++
		}
		out = append(out, 0x80|byte(count-1))
		out = append(out, src[start:start+count]...)
		i += count
	}
	return out
}

func ensureParentDir(path string) error {
	dir := filepath.Dir(path)
	if dir == "." || dir == "" {
		return nil
	}
	return os.MkdirAll(dir, 0o755)
}

func put16(b []byte, off int, v uint16) {
	binary.LittleEndian.PutUint16(b[off:off+2], v)
}

func put32(b []byte, off int, v uint32) {
	binary.LittleEndian.PutUint32(b[off:off+4], v)
}
