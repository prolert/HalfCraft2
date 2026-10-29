"""the python tools' own checks, as tools/debug/core_tests.cpp has the c++ core's: plain checks over tools\\halfcraft
with no game. make core-test runs them after the core's.

python tools/debug/tool_tests.py

check(condition, what) counts a check and, when it fails, says which one and goes on: one run shows everything
wrong. each test is a function named for what it holds. exits 0 when every check passes, 1 when one fails.
"""

import importlib.util
import os
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, engines, link, protocol

RESULTS: list[bool] = []
HL2DM = engines.ENGINES["hl2dm"]
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


def game_tool(name: str) -> ModuleType:
    """tools/game/<name>.py imported afresh (its module-level code runs again), without running its main."""
    spec = importlib.util.spec_from_file_location(name, REPO / f"tools/game/{name}.py")
    if spec is None or spec.loader is None:
        raise ToolError(f"no tools/game/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LinkVariable:
    """HALFCRAFT_LINK set to a value (None: unset) for a while, then as it was."""

    def __init__(self, value: str | None) -> None:
        self.value = value
        self.saved = os.environ.get(link.LINK_VARIABLE)

    def __enter__(self) -> None:
        os.environ.pop(link.LINK_VARIABLE, None)
        if self.value is not None:
            os.environ[link.LINK_VARIABLE] = self.value

    def __exit__(self, *_: object) -> None:
        os.environ.pop(link.LINK_VARIABLE, None)
        if self.saved is not None:
            os.environ[link.LINK_VARIABLE] = self.saved


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


def test_file_tag() -> None:
    check(link.file_tag("guest") == "guest", "guest's tag is guest")
    check(link.file_tag("a_Z-9") == "a_~z-9", f"a_Z-9's tag is {link.file_tag('a_Z-9')}")
    # ids that differ in case alone (two links in the kernel's names) give tags that differ even on ntfs
    for ids in (("b", "B"), ("ab", "aB", "Ab", "AB")):
        tags = [link.file_tag(link_id) for link_id in ids]
        check(len({tag.lower() for tag in tags}) == len(ids), f"{ids}: tags {tags} differ ignoring case")
        check(all(tag == tag.lower() for tag in tags), f"{ids}: tags {tags} have no uppercase letter")
    for bad in ("", "b.c", "b c"):
        check(raises(lambda bad=bad: link.file_tag(bad)), f"{bad!r} has no tag")


def test_launch_arguments() -> None:
    run_hl2 = game_tool("run_hl2")

    def arguments(link_id: str | None) -> list[str]:
        return run_hl2.launch_arguments(HL2DM, link=link_id, fullscreen=False, width=1280, height=720, map="d1_trainstation_02", extra=["+sv_cheats", "1"])

    def follows(found: list[str], first: str, then: str) -> bool:
        return any(found[at : at + 2] == [first, then] for at in range(len(found) - 1))

    default = arguments(None)
    check("-multirun" in default and "-condebug" in default, f"the default link's copy: -multirun and -condebug ({default})")
    check("-hc_link" not in default and "+con_logfile" not in default, f"the default link's copy: no -hc_link, no +con_logfile ({default})")
    check(default[-2:] == ["+map", "d1_trainstation_02"] and follows(default, "+sv_cheats", "1"), f"the map last, after the extra arguments ({default})")
    guest = arguments("guest")
    check("-multirun" in guest and follows(guest, "-hc_link", "guest"), f"guest's copy: -multirun -hc_link guest ({guest})")
    check(
        follows(guest, "+con_logfile", "console-guest.log") and "-condebug" not in guest,
        f"guest's copy: +con_logfile console-guest.log, no -condebug ({guest})",
    )
    check(follows(arguments("B"), "+con_logfile", "console-~b.log"), "B's copy logs to console-~b.log")
    full = run_hl2.launch_arguments(HL2DM, link=None, fullscreen=True, width=1, height=1, map=None, extra=[])
    check("-windowed" not in full and "+map" not in full and "-multirun" in full, f"fullscreen, no map: -multirun still ({full})")


def test_bad_link_ids_raise() -> None:
    run_hl2 = game_tool("run_hl2")
    for bad in ("", "b.c"):
        check(
            raises(lambda bad=bad: run_hl2.launch_arguments(HL2DM, link=bad, fullscreen=False, width=1, height=1, map=None, extra=[])),
            f"launch_arguments refuses {bad!r}",
        )
        check(raises(lambda bad=bad: engines.pid_file(HL2DM, bad)), f"pid_file refuses {bad!r}")
        check(raises(lambda bad=bad: engines.game_pid(HL2DM, bad)), f"game_pid refuses {bad!r}")
        check(raises(lambda bad=bad: engines.game_on_link(bad)), f"game_on_link refuses {bad!r}")


def test_pid_file() -> None:
    check(engines.pid_file(HL2DM, None) == REPO / "build/game-hl2dm/hl2.pid", f"the default link's: {engines.pid_file(HL2DM, None)}")
    check(engines.pid_file(HL2DM, "guest") == REPO / "build/game-hl2dm/hl2-guest.pid", f"guest's: {engines.pid_file(HL2DM, 'guest')}")
    check(engines.pid_file(HL2DM, "B").name == "hl2-~b.pid", f"B's: {engines.pid_file(HL2DM, 'B').name}")


def test_live_pid() -> None:
    python = Path(sys.executable)
    other_exe = Path(os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe"))
    with tempfile.TemporaryDirectory() as folder:
        file = Path(folder) / "hl2.pid"
        check(engines.live_pid(file, python) is None, "no file: None")
        file.write_text("not a pid\n", encoding="ascii")
        check(engines.live_pid(file, python) is None, "no number in the file: None")
        with subprocess.Popen([sys.executable, "-c", "pass"]) as finished:
            finished.wait()
            file.write_text(f"{finished.pid}\n", encoding="ascii")
            check(engines.live_pid(file, python) is None, "a finished child's pid: None")
        file.write_text(f"{os.getpid()}\n", encoding="ascii")
        check(engines.live_pid(file, other_exe) is None, "a live pid of another exe: None")
        check(engines.live_pid(file, python) == os.getpid(), "this process with its own exe: its pid")
        # a file written before the process started names another process that had the pid then
        os.utime(file, ns=(0, 0))
        check(engines.live_pid(file, python) is None, "a file older than the process: None")


def test_session_imports_lazily() -> None:
    with LinkVariable("b c"):
        try:
            test_session = game_tool("test_session")
        except SystemExit as error:
            check(False, f"test_session imports with HALFCRAFT_LINK='b c' (it exited: {error})")
            return
        check(True, "test_session imports with HALFCRAFT_LINK='b c'")
        check(raises(lambda: test_session.MINECRAFT_MUTEX), "MINECRAFT_MUTEX with HALFCRAFT_LINK='b c': a ToolError")
    with LinkVariable("b"):
        mutex = test_session.MINECRAFT_MUTEX
        check(mutex == "Local\\HalfCraft_v1.b_minecraft", f"MINECRAFT_MUTEX with HALFCRAFT_LINK=b: {mutex}")
    with LinkVariable(None):
        mutex = test_session.MINECRAFT_MUTEX
        check(mutex == "Local\\HalfCraft_v1_minecraft", f"MINECRAFT_MUTEX with no HALFCRAFT_LINK: {mutex}")
    # what tools/game/run_minecraft.py imports
    for name in ("Session", "ensure_minecraft_stopped", "with_session"):
        check(callable(getattr(test_session, name, None)), f"test_session.{name} is there")
    check(not hasattr(test_session, "NOT_THERE"), "another name is no attribute")


def main() -> int:
    test_valid_link_id()
    test_link_names()
    test_this_link()
    test_file_tag()
    test_launch_arguments()
    test_bad_link_ids_raise()
    test_pid_file()
    test_live_pid()
    test_session_imports_lazily()
    failed = RESULTS.count(False)
    if failed == 0:
        print(f"tool_tests: {len(RESULTS)} checks passed")
        return 0
    print(f"tool_tests: {failed} of {len(RESULTS)} checks failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
