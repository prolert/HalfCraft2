"""sends console commands to the running game (make run, either engine), one after another:

python tools/game/hl2_command.py "save test" "hc_look 75 0" "hc_click 3" "load test"

each command goes through a cfg file and +exec over -hijack (-hijack alone drops some arguments). output lands
in the game's console.log. useful where typing into the console isn't (synthetic keys, keyboard layouts).
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError, commands, engines, windows

SECONDS = 15
# the game takes the handed-over command a moment after the hand-over ends
SETTLE_SECONDS = 1.5


def leftovers(exe: Path, game: windows.Process, since: int) -> list[windows.Process]:
    """the instances of this very exe, apart from the game, that started at or after since (windows' ticks): what a hand-over left behind."""
    return [
        other
        for other in windows.processes(exe.name)
        if other.pid != game.pid and other.started is not None and other.started >= since and windows.runs_exe(other.pid, exe)
    ]


def send(engine: engines.Engine, exe: Path, game: windows.Process, command: str) -> None:
    (engine.game / "cfg/hc_command.cfg").write_bytes(f"{command}\r\n".encode("ascii", errors="replace"))
    handed_over = windows.now()
    hijack = subprocess.Popen(
        [exe, "-game", engine.game, "-hijack", "+exec", "hc_command"],
        cwd=exe.parent,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        hijack.wait(timeout=SECONDS)
    except subprocess.TimeoutExpired:
        commands.kill_tree(hijack.pid)
        raise ToolError(f"the command '{command}' didn't reach the game") from None
    time.sleep(SETTLE_SECONDS)
    # a hand-over sometimes leaves a second instance behind with "only one instance" on screen. only one this hand-over started
    # goes: another project's hl2.exe (a private-engine build) may run beside the game, and it is no business of these tools
    for other in leftovers(exe, game, handed_over):
        commands.kill_tree(other.pid)


def main() -> None:
    parser = argparse.ArgumentParser(description="send console commands to the running game")
    parser.add_argument("commands", nargs="+", metavar="command")
    options = parser.parse_args()

    running = engines.running()
    if not running:
        raise ToolError("the game isn't running (make run)")
    engine, game = running
    # -hijack finds the game's window only on its own desktop: from the other one the hand-over would start a second
    # game there, with its "only one instance" box, and the command would never arrive
    hidden = any(pid == game.pid for _, pid in windows.windows_on_hidden_desktop())
    sent_from_hidden = bool(os.environ.get(windows.HIDDEN_DESKTOP_ENV))
    if hidden and not sent_from_hidden:
        raise ToolError(f"the game runs on the {windows.HIDDEN_DESKTOP} desktop: send it through python tools/game/hidden_desktop.py")
    if sent_from_hidden and not hidden:
        raise ToolError("the game runs on your desktop: use plain make cmd (or tools/game/hl2_command.py) without tools/game/hidden_desktop.py")
    exe = engine.find_exe()
    for command in options.commands:
        send(engine, exe, game, command)
        print(f"sent: {command}")


if __name__ == "__main__":
    main()
