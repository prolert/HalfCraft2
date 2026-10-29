"""sends console commands to the running game (make run, either engine), one after another:

python tools/game/hl2_command.py "save test" "hc_look 75 0" "hc_click 3" "load test"
python tools/game/hl2_command.py --link guest "connect 192.168.1.5"     the copy make run started on link guest

a game make run started (its pid in the game folder: hl2.pid, or hl2-<tag>.pid on a link, tools/halfcraft/engines.py)
gets each command typed into its console (windows.send_console: WM_COPYDATA to that pid's window), from either
desktop: this one or tools/game/hidden_desktop.py's. any other game on the default link (HalfCraft.exe's, one started
by hand) gets it through a cfg file and +exec over -hijack (-hijack alone drops some arguments), which reaches a
game on its own desktop only and can't tell two copies apart. output lands in the copy's console log (console.log,
or console-<tag>.log on a link). useful where typing into the console isn't (synthetic keys, keyboard layouts).
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
# between two commands typed into a copy's console
TYPED_SECONDS = 0.2


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


def hijack(command_list: list[str]) -> None:
    """the commands through -hijack, to the game on the default link that make run didn't start, when it runs alone."""
    unrecorded = engines.unrecorded_games()
    if not unrecorded:
        raise ToolError("the game isn't running on the default link (make run)")
    if len(running := engines.games()) > 1:
        pids = ", ".join(str(game.pid) for _, game in running)
        raise ToolError(f"{len(running)} games run ({pids}), and make run didn't start the default link's: -hijack can't tell them apart")
    engine, game = unrecorded[0]
    # -hijack finds the game's window only on its own desktop: from the other one the hand-over would start a second
    # game there, with its "only one instance" box, and the command would never arrive
    hidden = any(pid == game.pid for _, pid in windows.windows_on_hidden_desktop())
    sent_from_hidden = bool(os.environ.get(windows.HIDDEN_DESKTOP_ENV))
    if hidden and not sent_from_hidden:
        raise ToolError(f"the game runs on the {windows.HIDDEN_DESKTOP} desktop: send it through python tools/game/hidden_desktop.py")
    if sent_from_hidden and not hidden:
        raise ToolError("the game runs on your desktop: use plain make cmd (or tools/game/hl2_command.py) without tools/game/hidden_desktop.py")
    exe = engine.find_exe()
    for command in command_list:
        send(engine, exe, game, command)
        print(f"sent: {command}")


def main() -> None:
    parser = argparse.ArgumentParser(description="send console commands to the running game")
    parser.add_argument("--link", help="the link id of the copy make run started (none: the default link's)")
    parser.add_argument("commands", nargs="+", metavar="command")
    options = parser.parse_args()

    if found := engines.game_on_link(options.link):
        pid = found[1]
        for index, command in enumerate(options.commands):
            if index:
                time.sleep(TYPED_SECONDS)
            windows.send_console(pid, command)
            print(f"sent: {command}")
        return
    if options.link is not None:
        raise ToolError(f"no game make run started on link {options.link} runs (make run LINK={options.link})")
    hijack(options.commands)


if __name__ == "__main__":
    main()
