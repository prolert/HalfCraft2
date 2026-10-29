"""the python tools' own checks, as tools/debug/core_tests.cpp has the c++ core's: plain checks over tools\\halfcraft
with no game. make core-test runs them after the core's.

python tools/debug/tool_tests.py

check(condition, what) counts a check and, when it fails, says which one and goes on: one run shows everything
wrong. each test is a function named for what it holds. exits 0 when every check passes, 1 when one fails.
"""

import os
import sys
from collections.abc import Callable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError, link, protocol

RESULTS: list[bool] = []
OTHER_LETTERS = (chr(0xE9), chr(0xFF22), chr(0x431))  # an e with an acute accent, a fullwidth B, a cyrillic b


def check(condition: bool, what: str) -> None:
    RESULTS.append(condition)
    if not condition:
        print(f"FAILED {what}")


def raises(call: Callable[[], object]) -> bool:
    """whether call fails the way a tool explains itself (ToolError)."""
    try:
        call()
    except ToolError:
        return True
    return False


def test_valid_link_id() -> None:
    for good in ("b", "AZaz09_-", "x" * link.MAX_LINK_ID):
        check(link.valid_link_id(good), f"{good!r} is a link id")
    # a mapping name's own characters, a space, a line break, other letters and a nul aren't an id's
    for bad in ("", "x" * (link.MAX_LINK_ID + 1), "b c", "a.b", "a/b", "a\\b", "a:b", "*", "b\n", *OTHER_LETTERS, "a\0b"):
        check(not link.valid_link_id(bad), f"{bad!r} isn't a link id")


def test_link_names() -> None:
    default = str(protocol.load().constants["kMappingName"].value)
    # no id: the names halfcraft always had
    for none in (None, ""):
        names = link.link_names(default, none)
        check(names.id == "", f"{none!r}: the default link")
        check(names.mapping == default == "Local\\HalfCraft_v1", f"{none!r}: mapping {names.mapping}")
        check(names.frame_event == "Local\\HalfCraft_v1_frame", f"{none!r}: frame event {names.frame_event}")
        check(names.minecraft_mutex == "Local\\HalfCraft_v1_minecraft", f"{none!r}: mutex {names.minecraft_mutex}")
    # an id: the default mapping's name, a dot and the id, then the event and the mutex after it
    names = link.link_names(default, "b")
    check(names.id == "b" and names.mapping == "Local\\HalfCraft_v1.b", f"b: mapping {names.mapping}")
    check(names.frame_event == "Local\\HalfCraft_v1.b_frame", f"b: frame event {names.frame_event}")
    check(names.minecraft_mutex == "Local\\HalfCraft_v1.b_minecraft", f"b: mutex {names.minecraft_mutex}")
    longest = "Pair_2-of-3_ABCDEFGHIJKLMNOPQRST"
    check(len(longest) == link.MAX_LINK_ID and link.link_names(default, longest).mapping == f"Local\\HalfCraft_v1.{longest}", "the longest id")
    # not an id: an error, never the default link's names
    for bad in ("b c", "x" * (link.MAX_LINK_ID + 1), OTHER_LETTERS[1]):
        check(raises(lambda bad=bad: link.link_names(default, bad)), f"{bad!r} gives no names")


def test_this_link() -> None:
    schema = protocol.load()
    saved = os.environ.pop(link.LINK_VARIABLE, None)
    try:
        check(link.this_link(schema).mapping == "Local\\HalfCraft_v1", "HALFCRAFT_LINK unset: the default link")
        os.environ[link.LINK_VARIABLE] = "b"
        check(link.this_link(schema).mapping == "Local\\HalfCraft_v1.b", "HALFCRAFT_LINK=b: link b")
        check(link.this_link(schema, "").mapping == "Local\\HalfCraft_v1", "an id given wins over HALFCRAFT_LINK")
        os.environ[link.LINK_VARIABLE] = "b c"
        check(raises(lambda: link.this_link(schema)), "HALFCRAFT_LINK='b c': no link")
    finally:
        os.environ.pop(link.LINK_VARIABLE, None)
        if saved is not None:
            os.environ[link.LINK_VARIABLE] = saved


def main() -> int:
    test_valid_link_id()
    test_link_names()
    test_this_link()
    failed = RESULTS.count(False)
    if failed == 0:
        print(f"tool_tests: {len(RESULTS)} checks passed")
        return 0
    print(f"tool_tests: {failed} of {len(RESULTS)} checks failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
