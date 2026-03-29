#!/usr/bin/env python3
"""Embed fast device thumbnails in EPUB files.

The reader extracts META-INF/thumbnail.jpg before it tries to decode an EPUB
cover on-device. This tool finds the EPUB cover, produces the same 225 x 340
maximum JPEG thumbnail used by the firmware, then stores it at that path.

Requires Pillow:
    python3 -m pip install Pillow
"""

from __future__ import annotations

import argparse
import io
import posixpath
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Iterable
from urllib.parse import unquote
from xml.etree import ElementTree

try:
    from PIL import Image, ImageOps
except ImportError:
    print("Pillow is required. Install it with: python3 -m pip install Pillow", file=sys.stderr)
    sys.exit(2)


THUMBNAIL_PATH = "META-INF/thumbnail.jpg"
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif"}


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def resolve_epub_path(opf_path: str, href: str) -> str:
    href = unquote(href.split("#", 1)[0]).replace("\\", "/")
    return posixpath.normpath(posixpath.join(posixpath.dirname(opf_path), href)).lstrip("/")


def member_name(names: set[str], path: str) -> str | None:
    if path in names:
        return path
    folded = {name.casefold(): name for name in names}
    return folded.get(path.casefold())


def cover_path(epub: zipfile.ZipFile) -> str:
    names = {info.filename for info in epub.infolist() if not info.is_dir()}
    try:
        container = ElementTree.fromstring(epub.read("META-INF/container.xml"))
    except (KeyError, ElementTree.ParseError) as error:
        raise ValueError("EPUB has no readable META-INF/container.xml") from error

    root_file = next((item.get("full-path") for item in container.iter() if local_name(item.tag) == "rootfile"), None)
    if not root_file:
        raise ValueError("EPUB container does not define an OPF package")
    root_file = member_name(names, root_file)
    if not root_file:
        raise ValueError("EPUB package file is missing")

    try:
        opf = ElementTree.fromstring(epub.read(root_file))
    except ElementTree.ParseError as error:
        raise ValueError("EPUB package file is invalid") from error

    manifest = [item for item in opf.iter() if local_name(item.tag) == "item"]
    by_id = {item.get("id", ""): item for item in manifest}

    cover_id = next(
        (
            item.get("content", "")
            for item in opf.iter()
            if local_name(item.tag) == "meta" and item.get("name", "").lower() == "cover"
        ),
        "",
    )
    candidates = []
    if cover_id in by_id:
        candidates.append(by_id[cover_id])
    candidates.extend(item for item in manifest if "cover-image" in item.get("properties", "").split())
    candidates.extend(
        item
        for item in manifest
        if item.get("id", "").lower() in {"cover", "cover-image"}
        or "cover" in item.get("href", "").lower()
    )
    candidates.extend(item for item in manifest if item.get("media-type", "").lower().startswith("image/"))

    seen: set[str] = set()
    for item in candidates:
        href = item.get("href", "")
        path = resolve_epub_path(root_file, href)
        if not href or path in seen:
            continue
        seen.add(path)
        actual = member_name(names, path)
        if actual and Path(actual).suffix.lower() in IMAGE_EXTENSIONS:
            return actual

    raise ValueError("EPUB does not contain a usable cover image")


def thumbnail_jpeg(image_data: bytes, width: int, height: int, quality: int) -> bytes:
    with Image.open(io.BytesIO(image_data)) as source:
        image = ImageOps.exif_transpose(source)
        image.thumbnail((width, height), Image.Resampling.LANCZOS)
        if image.mode in {"RGBA", "LA"} or "transparency" in image.info:
            image = image.convert("RGBA")
            background = Image.new("RGB", image.size, "white")
            background.paste(image.convert("RGB"), mask=image.getchannel("A"))
            image = background
        elif image.mode != "RGB":
            image = image.convert("RGB")

        output = io.BytesIO()
        image.save(output, format="JPEG", quality=quality, optimize=True, progressive=True)
        return output.getvalue()


def write_epub(source_path: Path, output_path: Path, image_data: bytes) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output_path.parent, suffix=".epub", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with zipfile.ZipFile(source_path, "r") as source, zipfile.ZipFile(
            temporary_path, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=True
        ) as output:
            output.comment = source.comment
            infos = source.infolist()
            mimetype = next((info for info in infos if info.filename == "mimetype"), None)
            if mimetype:
                output.writestr("mimetype", source.read(mimetype), compress_type=zipfile.ZIP_STORED)
            for info in infos:
                if info.is_dir() or info.filename in {"mimetype", THUMBNAIL_PATH}:
                    continue
                output.writestr(info, source.read(info), compress_type=info.compress_type)
            output.writestr(THUMBNAIL_PATH, image_data, compress_type=zipfile.ZIP_DEFLATED)
        temporary_path.replace(output_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise


def epub_files(target: Path, recursive: bool) -> Iterable[Path]:
    if target.is_file():
        if target.suffix.lower() != ".epub":
            raise ValueError("Input file must be an EPUB")
        return [target]
    if not target.is_dir():
        raise ValueError("Input path does not exist")
    iterator = target.rglob("*.epub") if recursive else target.glob("*.epub")
    return sorted(path for path in iterator if not path.name.endswith(".thumbnail.epub"))


def output_path(source: Path, args: argparse.Namespace, file_count: int) -> Path:
    if args.in_place:
        return source
    if args.output:
        requested = Path(args.output)
        if file_count == 1:
            return requested
        return requested / f"{source.stem}.thumbnail.epub"
    return source.with_name(f"{source.stem}.thumbnail.epub")


def process(source: Path, destination: Path, args: argparse.Namespace) -> None:
    with zipfile.ZipFile(source, "r") as epub:
        cover = cover_path(epub)
        image = thumbnail_jpeg(epub.read(cover), args.width, args.height, args.quality)
    write_epub(source, destination, image)
    print(f"{source.name}: {cover} -> {destination.name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Embed META-INF/thumbnail.jpg in EPUB files for fast Inx device thumbnail generation."
    )
    parser.add_argument("path", help="An EPUB file or folder containing EPUB files")
    parser.add_argument("--recursive", action="store_true", help="Include EPUBs in subfolders")
    parser.add_argument("--in-place", action="store_true", help="Atomically replace each source EPUB")
    parser.add_argument("--output", help="Output EPUB for one source, or output folder for multiple sources")
    parser.add_argument("--width", type=int, default=225, help="Maximum thumbnail width (default: 225)")
    parser.add_argument("--height", type=int, default=340, help="Maximum thumbnail height (default: 340)")
    parser.add_argument("--quality", type=int, default=92, help="JPEG quality from 1 to 95 (default: 92)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.width < 1 or args.height < 1:
        print("Thumbnail width and height must be positive.", file=sys.stderr)
        return 2
    if not 1 <= args.quality <= 95:
        print("JPEG quality must be between 1 and 95.", file=sys.stderr)
        return 2
    if args.in_place and args.output:
        print("Use either --in-place or --output, not both.", file=sys.stderr)
        return 2

    try:
        sources = list(epub_files(Path(args.path).expanduser(), args.recursive))
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    if not sources:
        print("No EPUB files found.", file=sys.stderr)
        return 1

    failures = 0
    for source in sources:
        try:
            process(source, output_path(source, args, len(sources)), args)
        except (OSError, ValueError, zipfile.BadZipFile) as error:
            failures += 1
            print(f"{source.name}: failed: {error}", file=sys.stderr)

    print(f"Completed {len(sources) - failures}/{len(sources)} EPUB thumbnail(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
