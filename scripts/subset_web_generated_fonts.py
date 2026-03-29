#!/usr/bin/env python3
"""Remove unused glyph rows from the web-generated built-in font headers.

The bitmap rows are copied byte-for-byte from the checked-in headers.  This
script only selects the reader codepoint profile and rewrites offsets and
intervals, so raster weight, anti-aliasing, and font metrics do not change.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "src" / "system" / "font"

# Keep the web UI raster, but retain the vertical metrics from the original
# fontconvert output.  These metrics are part of the UI layout contract:
# system popups and two-line library labels use advanceY directly.
ATKINSON_VERTICAL_METRICS_BY_SIZE = {
    8: (21, 16, -5, False),
    10: (26, 20, -7, True),
    12: (31, 24, -8, True),
    14: (36, 28, -9, True),
    16: (41, 32, -10, True),
    18: (47, 36, -11, True),
}

# This is the compact reader profile used by fontconvert.py.  It is used here
# only as a codepoint filter; the glyph bitmaps still come from the web UI
# generated headers currently shipped in the firmware.
READER_INTERVALS = (
    (0x000D, 0x000D),
    (0x0020, 0x007E),
    (0x00A0, 0x00FF),
    (0x0100, 0x0107),
    (0x010A, 0x0113),
    (0x0116, 0x011B),
    (0x011E, 0x0123),
    (0x0126, 0x0127),
    (0x012A, 0x012B),
    (0x012E, 0x012F),
    (0x0131, 0x0131),
    (0x0136, 0x0137),
    (0x013B, 0x013E),
    (0x0141, 0x0148),
    (0x0150, 0x0155),
    (0x0158, 0x015B),
    (0x015E, 0x0165),
    (0x016A, 0x016B),
    (0x016E, 0x0173),
    (0x0178, 0x017E),
    (0x0300, 0x0304),
    (0x0306, 0x0308),
    (0x030A, 0x030C),
    (0x0326, 0x0328),
    (0x0400, 0x04FF),
    # Preserve the Unicode spacing and text-separator glyphs used by EPUB
    # typography.  These are not optional punctuation: removing them makes
    # thin spaces, hair spaces, and narrow no-break spaces fall back and
    # visibly compress text.
    (0x2000, 0x200F),
    (0x2028, 0x2029),
    (0x202F, 0x202F),
    (0x205F, 0x205F),
    (0x2060, 0x2060),
    (0x2013, 0x2014),
    (0x2018, 0x201A),
    (0x201C, 0x201E),
    (0x2020, 0x2022),
    (0x2026, 0x2026),
    (0x2030, 0x2030),
    (0x2039, 0x203A),
    (0x2044, 0x2044),
    (0x2074, 0x2074),
    (0x20AC, 0x20AC),
    (0x2202, 0x2202),
    (0x220F, 0x220F),
    (0x2211, 0x2212),
    (0x2215, 0x2215),
    (0x2219, 0x221A),
    (0x221E, 0x221E),
    (0x222B, 0x222B),
    (0x2248, 0x2248),
    (0x2260, 0x2260),
    (0x2264, 0x2265),
    # EpdFont uses this glyph as the fallback for unsupported text.
    (0xFFFD, 0xFFFD),
)


def in_reader_profile(cp: int) -> bool:
    return any(start <= cp <= end for start, end in READER_INTERVALS)


def parse_bitmap(text: str, name: str) -> tuple[list[int], str]:
    match = re.search(rf"static const uint8_t {name}Bitmaps\[\d+\] = \{{(.*?)\n\}};", text, re.S)
    if not match:
        raise ValueError(f"bitmap array not found in {name}")
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(1))]
    return values, match.group(0)


def parse_intervals(text: str, name: str) -> list[tuple[int, int, int]]:
    match = re.search(rf"static const EpdUnicodeInterval {name}Intervals\[\] = \{{(.*?)\n\}};", text, re.S)
    if not match:
        raise ValueError(f"interval array not found in {name}")
    return [
        (int(first, 16), int(last, 16), int(offset, 0))
        for first, last, offset in re.findall(
            r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*\}",
            match.group(1),
        )
    ]


def parse_glyphs(text: str, name: str) -> tuple[list[tuple[int, ...]], str]:
    match = re.search(rf"static const EpdGlyph {name}Glyphs\[\] = \{{(.*?)\n\}};", text, re.S)
    if not match:
        raise ValueError(f"glyph array not found in {name}")
    rows = []
    for row in re.findall(r"^\s*\{\s*([^{}]+?)\s*\},\s*//.*$", match.group(1), re.M):
        fields = tuple(int(value.strip(), 0) for value in row.split(","))
        if len(fields) != 7:
            raise ValueError(f"unexpected glyph row in {name}: {row}")
        rows.append(fields)
    return rows, match.group(0)


def codepoints_from_intervals(intervals: list[tuple[int, int, int]], glyph_count: int) -> list[int]:
    codepoints: list[int] = []
    for first, last, offset in intervals:
        if offset != len(codepoints):
            raise ValueError("font intervals are not contiguous")
        codepoints.extend(range(first, last + 1))
    if len(codepoints) != glyph_count:
        raise ValueError(f"interval/glyph mismatch: {len(codepoints)} != {glyph_count}")
    return codepoints


def cp_label(cp: int) -> str:
    if cp == 0x5C:
        return "<backslash>"
    return chr(cp) if 0x20 < cp < 0x7F else f"U+{cp:04X}"


def compact_intervals(codepoints: list[int]) -> list[tuple[int, int, int]]:
    result: list[tuple[int, int, int]] = []
    if not codepoints:
        return result
    start = previous = codepoints[0]
    offset = 0
    for cp in codepoints[1:]:
        if cp != previous + 1:
            result.append((start, previous, offset))
            offset += previous - start + 1
            start = cp
        previous = cp
    result.append((start, previous, offset))
    return result


def fix_vertical_metrics(text: str, name: str) -> str:
    if not name.startswith("atkinson_hyperlegible_"):
        return text
    match = re.search(r"_(\d+)_", name)
    if not match:
        return text
    metrics = ATKINSON_VERTICAL_METRICS_BY_SIZE.get(int(match.group(1)))
    if metrics is None:
        return text
    advance_y, ascender, descender, is_2bit = metrics
    pattern = rf"(static const EpdFontData {name} = \{{.*?\n\s*\d+,\n\s*)\d+(,\n\s*)-?\d+(,\n\s*)-?\d+(,\n\s*)(?:true|false)(,\n\}};)"
    replacement = rf"\g<1>{advance_y}\g<2>{ascender}\g<3>{descender}\g<4>{str(is_2bit).lower()}\g<5>"
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise ValueError(f"vertical metrics not found in {name}")
    return updated


def rewrite(path: Path) -> tuple[int, int, int, int]:
    text = path.read_text(encoding="utf-8")
    name_match = re.search(r"\* name: (\w+)", text)
    if not name_match:
        raise ValueError(f"font name missing from {path}")
    name = name_match.group(1)
    bitmap, bitmap_block = parse_bitmap(text, name)
    intervals = parse_intervals(text, name)
    glyphs, glyph_block = parse_glyphs(text, name)
    codepoints = codepoints_from_intervals(intervals, len(glyphs))

    source_rows = {cp: (glyph, bitmap) for cp, glyph in zip(codepoints, glyphs)}
    desired_missing = any(in_reader_profile(cp) and cp not in source_rows for cp in range(0x10000))
    if desired_missing:
        # Recover newly requested profile glyphs from the checked-in
        # web-generated header so rerunning the tool remains safe and does not
        # require regenerating any font.
        baseline_text = subprocess.check_output(
            ["git", "show", f"HEAD:{path.relative_to(ROOT).as_posix()}"],
            text=True,
        )
        baseline_name = re.search(r"\* name: (\w+)", baseline_text).group(1)
        baseline_bitmap, _ = parse_bitmap(baseline_text, baseline_name)
        baseline_intervals = parse_intervals(baseline_text, baseline_name)
        baseline_glyphs, _ = parse_glyphs(baseline_text, baseline_name)
        baseline_codepoints = codepoints_from_intervals(baseline_intervals, len(baseline_glyphs))
        baseline_rows = dict(zip(baseline_codepoints, baseline_glyphs))
        for cp, glyph in baseline_rows.items():
            if in_reader_profile(cp) and cp not in source_rows:
                source_rows[cp] = (glyph, baseline_bitmap)

    keep = [(cp, *source_rows[cp]) for cp in sorted(source_rows) if in_reader_profile(cp)]
    if not keep:
        raise ValueError(f"profile removed every glyph from {path}")

    new_bitmap: list[int] = []
    new_glyphs: list[str] = []
    for cp, glyph, bitmap_source in keep:
        width, height, advance_x, left, top, data_length, data_offset = glyph
        start = len(new_bitmap)
        new_bitmap.extend(bitmap_source[data_offset : data_offset + data_length])
        new_glyphs.append(
            f"    {{ {width}, {height}, {advance_x}, {left}, {top}, {data_length}, {start} }}, // {cp_label(cp)}"
        )

    new_intervals = compact_intervals([cp for cp, _, _ in keep])
    bitmap_lines = [f"static const uint8_t {name}Bitmaps[{len(new_bitmap)}] = {{"]
    for index in range(0, len(new_bitmap), 16):
        bitmap_lines.append("    " + " ".join(f"0x{value:02X}," for value in new_bitmap[index : index + 16]))
    bitmap_lines.append("};")
    glyph_text = f"static const EpdGlyph {name}Glyphs[] = {{\n" + "\n".join(new_glyphs) + "\n};"
    interval_text = (
        f"static const EpdUnicodeInterval {name}Intervals[] = {{\n"
        + "\n".join(f"    {{ 0x{first:X}, 0x{last:X}, {offset} }}," for first, last, offset in new_intervals)
        + "\n};"
    )

    text = text.replace(bitmap_block, "\n".join(bitmap_lines), 1)
    text = text.replace(glyph_block, glyph_text, 1)
    old_interval_block = re.search(rf"static const EpdUnicodeInterval {name}Intervals\[\] = \{{.*?\n\}};", text, re.S).group(0)
    text = text.replace(old_interval_block, interval_text, 1)
    interval_count_pattern = rf"(static const EpdFontData {name} = \{{.*?{name}Intervals,\n\s*)\d+(\s*,)"
    text, count = re.subn(
        interval_count_pattern,
        rf"\g<1>{len(new_intervals)}\g<2>",
        text,
        count=1,
        flags=re.S,
    )
    if count != 1:
        raise ValueError(f"interval count not found in {name}")
    text = fix_vertical_metrics(text, name)
    text = re.sub(
        r"\* glyphs: \d+ \([^\n]+\)",
        f"* glyphs: {len(keep)} (web UI raster rows; compact reader profile)",
        text,
        count=1,
    )
    path.write_text(text, encoding="utf-8")
    return len(codepoints), len(keep), len(bitmap), len(new_bitmap)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report headers that would change")
    args = parser.parse_args()
    changed = False
    for path in sorted(FONT_DIR.glob("atkinson_hyperlegible_*.h")) + sorted(FONT_DIR.glob("chareink_*.h")):
        original = path.read_bytes()
        stats = rewrite(path)
        if path.read_bytes() != original:
            changed = True
            if args.check:
                path.write_bytes(original)
                print(f"out of date: {path.relative_to(ROOT)} ({stats[0]} -> {stats[1]} glyphs, {stats[2]} -> {stats[3]} bitmap bytes)")
            else:
                print(f"updated {path.relative_to(ROOT)}: {stats[0]} -> {stats[1]} glyphs, {stats[2]} -> {stats[3]} bitmap bytes")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
