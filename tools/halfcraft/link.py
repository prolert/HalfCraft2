"""the running game's shared memory for the debug tools (tools\\debug), read-only: the mapping, laid out as
protocol\\schema says, and what its collision ring still holds. and which link that is: a link id (HALFCRAFT_LINK,
which both games read too) gives a pair (a half-life and its minecraft) a link of its own, so two pairs can run on
one pc:

    no id    Local\\HalfCraft_v1      Local\\HalfCraft_v1_frame      Local\\HalfCraft_v1_minecraft
    id b     Local\\HalfCraft_v1.b    Local\\HalfCraft_v1.b_frame    Local\\HalfCraft_v1.b_minecraft

source/src/core/hc_link_names.h and minecraft's LinkNames.java build the same names (link_names here)."""

import mmap
import os
import re
import struct
from dataclasses import dataclass

from halfcraft import ToolError
from halfcraft.protocol import Schema

MESSAGE_HEADER = struct.Struct("<II")  # ColMsgHeader: type, payload bytes
VOID_KIND_BYTES = 512  # kColVoid's u8 kind[512] after its ColRegion: a VoidKind for each block of the region's 8x8x8
LINK_VARIABLE = "HALFCRAFT_LINK"  # the link id (source's -hc_link wins over it there)
MAX_LINK_ID = 32
_LINK_ID = re.compile(r"[A-Za-z0-9_-]{1,32}")


@dataclass(frozen=True)
class LinkNames:
    """a link's shared objects: its mapping, the host's frame event and the mutex minecraft holds while it runs."""

    id: str  # "" for the default link
    mapping: str
    frame_event: str
    minecraft_mutex: str


def valid_link_id(link_id: str) -> bool:
    """1 to MAX_LINK_ID characters, each of A-Z, a-z, 0-9, _ and -."""
    return _LINK_ID.fullmatch(link_id) is not None


def _bad_id(link_id: str) -> ToolError:
    return ToolError(f"{link_id!r} isn't a link id (1 to {MAX_LINK_ID} of A-Z, a-z, 0-9, _ and -): give a good one, or none for the default link")


def link_names(default_mapping: str, link_id: str | None) -> LinkNames:
    """the names of the link with this id ("" or None: the default link). a ToolError for an id that isn't one,
    never the default link's names: those are another pair's.
    default_mapping: the default link's mapping, the schema's kMappingName"""
    if not link_id:
        return LinkNames("", default_mapping, f"{default_mapping}_frame", f"{default_mapping}_minecraft")
    if not valid_link_id(link_id):
        raise _bad_id(link_id)
    mapping = f"{default_mapping}.{link_id}"
    return LinkNames(link_id, mapping, f"{mapping}_frame", f"{mapping}_minecraft")


def file_tag(link_id: str) -> str:
    """the link's id as it goes into file names (console-<tag>.log, hl2-<tag>.pid). ids are case-sensitive in the
    kernel's names (b and B are two links) and ntfs's file names aren't, so no two ids give tags that differ in case
    alone: lowercase letters, digits, _ and - stay as they are, and an uppercase letter is ~ and its lowercase (an id
    has no ~). guest stays guest, B is ~b. a ToolError for "" (the default link has no tag: its files keep their
    plain names) or anything else that isn't an id."""
    if not valid_link_id(link_id):
        raise _bad_id(link_id)
    return "".join(f"~{character.lower()}" if character.isupper() else character for character in link_id)


def this_link(schema: Schema, link_id: str | None = None) -> LinkNames:
    """the link with this id, or HALFCRAFT_LINK's when None (the way both games read it: the default link when it's
    unset or empty)."""
    default = str(schema.constants["kMappingName"].value)
    if link_id is not None:
        return link_names(default, link_id)
    try:
        return link_names(default, os.environ.get(LINK_VARIABLE))
    except ToolError as error:
        raise ToolError(f"{LINK_VARIABLE}: {error}") from None


def open_link(schema: Schema, link_id: str | None = None) -> mmap.mmap:
    """the mapping of the link with this id (HALFCRAFT_LINK's when None) read-only: an error when no game has it
    up, or one built from another schema does."""
    name = this_link(schema, link_id).mapping
    try:
        # a name nobody holds gets a new, empty mapping: the magic tells
        link = mmap.mmap(-1, schema.mapping_bytes, tagname=name, access=mmap.ACCESS_READ)
    except OSError as error:
        raise ToolError(f"can't map {name} ({error}): a game built from another schema, with another size, has it up?") from error
    header = schema.structs["Header"].read(link, schema.regions["Header"].at)
    if header["magic"] != schema.value("kMagic"):
        link.close()
        raise ToolError(f"no link in shared memory ({name}): is the game running?")
    if header["layout"] != schema.layout:
        link.close()
        raise ToolError(
            f"the game runs with protocol layout {header['layout']:#010x}, protocol/schema here is {schema.layout:#010x}: "
            "read it with the tools of the commit it was built from"
        )
    return link


@dataclass(frozen=True)
class Message:
    type: int  # ColType
    at: int  # its payload, in the mapping
    size: int  # the payload's bytes


class CollisionRing:
    """the collision ring as it is now. the host writes it a lap at a time from the ring's start (a message that
    won't fit before the end leaves a kColPad there), so a lap's start is a message's: the messages since the
    last one read from there, and what the lap before still holds from the first message that leads on to the
    lap's end. older ones are gone (minecraft still has them)."""

    def __init__(self, link: mmap.mmap, schema: Schema) -> None:
        ring = schema.regions["CollisionRing"].at
        self.link = link
        self.data = ring + int(schema.value("kColRingDataOff"))
        self.size = int(schema.value("kColRingDataBytes"))
        self.head = struct.unpack_from("<Q", link, ring + int(schema.value("kColRingHeadOff")))[0]
        self.tail = struct.unpack_from("<Q", link, ring + int(schema.value("kColRingTailOff")))[0]
        self.pad, self.clear, self.region, self.tris = (int(schema.value(name)) for name in ("kColPad", "kColClear", "kColRegion", "kColTris"))
        self.void = int(schema.value("kColVoid"))
        region = schema.structs["ColRegion"]
        self.region_bytes, self.count_at = region.size, region.field("count").offset
        self.item_bytes = {
            self.region: schema.structs["ColBlock"].size,
            self.tris: schema.structs["ColTri"].size,
            self.void: schema.structs["ColBlock"].field("bits").size,
        }
        self.kept_from = 0  # where what it holds starts (head - kept_from bytes of messages)

    def plausible(self, offset: int) -> int | None:
        """the bytes the message at `offset` in the ring takes up to the next one, when its header makes sense."""
        if offset + MESSAGE_HEADER.size > self.size:
            return None
        kind, size = MESSAGE_HEADER.unpack_from(self.link, self.data + offset)
        if kind == self.pad:
            return self.size - offset if size == 0 else None
        if offset + MESSAGE_HEADER.size + size > self.size:
            return None
        if kind == self.clear:
            fits = size == 4
        elif kind in self.item_bytes:
            count = struct.unpack_from("<I", self.link, self.data + offset + MESSAGE_HEADER.size + self.count_at)[0] if size >= self.region_bytes else -1
            fixed = self.region_bytes + (VOID_KIND_BYTES if kind == self.void else 0)
            fits = size == fixed + count * self.item_bytes[kind]
        else:
            fits = False
        return (MESSAGE_HEADER.size + size + 7) & ~7 if fits else None

    def walk(self, start: int, end: int) -> list[Message] | None:
        """the messages from `start` to `end` (bytes the host wrote in all), or None unless they lead there."""
        found: list[Message] = []
        position = start
        while position < end:
            offset = position % self.size
            step = self.plausible(offset)
            if step is None:
                return None
            kind, size = MESSAGE_HEADER.unpack_from(self.link, self.data + offset)
            if kind != self.pad:
                found.append(Message(kind, self.data + offset + MESSAGE_HEADER.size, size))
            position += step
        return found if position == end else None

    def messages(self) -> list[Message]:
        """what it holds, oldest first."""
        lap = self.head - self.head % self.size
        current = self.walk(lap, self.head)
        if current is None:
            raise ToolError(f"the collision ring doesn't read from its lap's start ({lap}) to its head ({self.head}): did the host write while it was read?")
        self.kept_from = lap
        if lap == 0:
            return current
        # the lap before, from where this one's writing ends: its first message that leads on to its end
        for start in range((self.head - self.size + 7) & ~7, lap, 8):
            if self.plausible(start % self.size) is not None and (older := self.walk(start, lap)) is not None:
                self.kept_from = start
                return older + current
        return current

    def epoch(self, message: Message) -> int:
        """a kColClear's."""
        return struct.unpack_from("<I", self.link, message.at)[0]
