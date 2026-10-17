"""the share of pixels two tga screenshots differ in, for checks in the game: the same view before and after a
change (a shader of halfcraft's against the stock one), and where they differ.

python tools/debug/tga_diff.py a.tga b.tga                    pixels whose r, g or b differ by more than 8
python tools/debug/tga_diff.py a.tga b.tga --tolerance 2      ... by more than 2
python tools/debug/tga_diff.py a.tga b.tga --max 0.5          exits 1 when more than 0.5 % of them differ
python tools/debug/tga_diff.py a.tga b.tga --out diff.tga     and a mask of them: white where they differ, black elsewhere

prints "a.tga vs b.tga: 0.125% of 921600 pixels differ by more than 8 (bounds x 10-200, y 300-310)", or
"(none)" when none does. the bounds are screen pixels, 0 0 at the top left. reads what source's screenshot
command writes: uncompressed or run-length tga (types 2 and 10), 24 or 32 bits (alpha isn't compared).
"""

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError

HEADER_BYTES = 18
TRUE_COLOR, TRUE_COLOR_RLE = 2, 10
TOP_LEFT = 0x20  # image descriptor bit 5: the first row stored is the top one
RIGHT_TO_LEFT = 0x10  # bit 4: each row stored from the right
RUN = 0x80  # a run-length packet: one pixel, repeated


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rows: list[bytes]  # top row first, 3 bytes (b, g, r) a pixel


def unpack_runs(data: bytes, at: int, pixels: int, size: int, path: Path) -> bytes:
    """a run-length image's pixels, unpacked."""
    out = bytearray()
    want = pixels * size
    while len(out) < want:
        if at >= len(data):
            raise ToolError(f"{path} ends in the middle of its pixels")
        packet = data[at]
        count = (packet & ~RUN) + 1
        if packet & RUN:
            out += data[at + 1 : at + 1 + size] * count
            at += 1 + size
        else:
            out += data[at + 1 : at + 1 + count * size]
            at += 1 + count * size
    return bytes(out[:want])


def read(path: Path) -> Image:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ToolError(f"can't read {path}: {error.strerror}") from None
    if len(data) < HEADER_BYTES:
        raise ToolError(f"{path} isn't a tga: {len(data)} bytes")
    id_length, color_map, kind = data[0], data[1], data[2]
    width, height = int.from_bytes(data[12:14], "little"), int.from_bytes(data[14:16], "little")
    depth, descriptor = data[16], data[17]
    if color_map or kind not in (TRUE_COLOR, TRUE_COLOR_RLE) or depth not in (24, 32) or descriptor & RIGHT_TO_LEFT:
        raise ToolError(f"{path} isn't a 24 or 32 bit true-colour tga (type {kind}, colour map {color_map}, {depth} bits, descriptor {descriptor:#x})")
    size = depth // 8
    at = HEADER_BYTES + id_length
    if kind == TRUE_COLOR:
        pixels = data[at : at + width * height * size]
        if len(pixels) < width * height * size:
            raise ToolError(f"{path} ends in the middle of its pixels")
    else:
        pixels = unpack_runs(data, at, width * height, size, path)
    if size == 4:  # alpha goes
        bgr = bytearray(width * height * 3)
        for channel in range(3):
            bgr[channel::3] = pixels[channel::4]
        pixels = bytes(bgr)
    stride = width * 3
    rows = [pixels[y * stride : (y + 1) * stride] for y in range(height)]
    if not descriptor & TOP_LEFT:
        rows.reverse()
    return Image(width, height, rows)


def differing(a: bytes, b: bytes, tolerance: int) -> list[int]:
    """the pixels of a row whose r, g or b differ by more than tolerance."""
    found = []
    for x in range(len(a) // 3):
        i = x * 3
        if abs(a[i] - b[i]) > tolerance or abs(a[i + 1] - b[i + 1]) > tolerance or abs(a[i + 2] - b[i + 2]) > tolerance:
            found.append(x)
    return found


def bounds(found: dict[int, list[int]]) -> str:
    """the box round the pixels that differ (by row: their x), or "none"."""
    if not found:
        return "none"
    return f"bounds x {min(min(xs) for xs in found.values())}-{max(max(xs) for xs in found.values())}, y {min(found)}-{max(found)}"


def write_mask(path: Path, width: int, height: int, rows: dict[int, list[int]]) -> None:
    """an uncompressed 24-bit tga, top row first: white where the images differ, black elsewhere."""
    pixels = bytearray(width * height * 3)
    for y, xs in rows.items():
        for x in xs:
            at = (y * width + x) * 3
            pixels[at : at + 3] = b"\xff\xff\xff"
    header = bytes((0, 0, TRUE_COLOR, 0, 0, 0, 0, 0, 0, 0, 0, 0)) + width.to_bytes(2, "little") + height.to_bytes(2, "little") + bytes((24, TOP_LEFT))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + pixels)


def main() -> int:
    parser = argparse.ArgumentParser(description="the share of pixels two tga screenshots differ in")
    parser.add_argument("a", type=Path)
    parser.add_argument("b", type=Path)
    parser.add_argument("--tolerance", type=int, default=8, help="how much r, g or b may differ by and still be the same (0-255)")
    parser.add_argument("--max", type=float, help="exit 1 when more than this percentage of the pixels differ")
    parser.add_argument("--out", type=Path, help="write a mask of where they differ: a tga, white there")
    options = parser.parse_args()

    a, b = read(options.a), read(options.b)
    if (a.width, a.height) != (b.width, b.height):
        raise ToolError(f"{options.a} is {a.width}x{a.height} and {options.b} {b.width}x{b.height}: they don't compare")
    found = {y: xs for y in range(a.height) if a.rows[y] != b.rows[y] and (xs := differing(a.rows[y], b.rows[y], options.tolerance))}
    count = sum(len(xs) for xs in found.values())
    pixels = a.width * a.height
    share = 100.0 * count / pixels if pixels else 0.0
    print(f"{options.a} vs {options.b}: {share:.3f}% of {pixels} pixels differ by more than {options.tolerance} ({bounds(found)})")
    if options.out:
        write_mask(options.out, a.width, a.height, found)
    return 1 if options.max is not None and share > options.max else 0


if __name__ == "__main__":
    raise SystemExit(main())
