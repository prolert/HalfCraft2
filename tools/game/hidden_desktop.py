"""runs a command on a desktop of its own that nobody sees (HalfCraftTest, in the session's window station), so a
test's games neither take the screen nor the player's keys and mouse:

python tools/game/hidden_desktop.py make mc-test                    the test client, hidden from the start
python tools/game/hidden_desktop.py make run MAP=d2_coast_03        half-life on it too
python tools/game/hidden_desktop.py make cmd C="'jpeg a 90'"        commands too: -hijack finds the game's window
                                                                    on its own desktop only
python tools/game/hidden_desktop.py --list                          the processes with a window there, with their
                                                                    exes and window titles (a box waiting there for a
                                                                    click shows up so: taskkill /F /PID ends it)
python tools/game/hidden_desktop.py --close <pid>                   asks a process there to close, as a click on its
                                                                    window's close button would (taskkill without /F
                                                                    doesn't reach minecraft's hidden window)

whatever the command starts lands there too (a process takes its parent's desktop), half-life's d3d9 and
minecraft's opengl both draw there, and `jpeg` writes what the game drew. gradle runs with --no-daemon there
(tools/halfcraft/gradle.py reads HALFCRAFT_DESKTOP): a daemon kept from this desktop would start the player's next
minecraft on it. the desktop lasts while something runs on it. prints the command's output and exits with its code.
"""

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError, windows

DESKTOP = windows.HIDDEN_DESKTOP

_user32 = ctypes.WinDLL("user32", use_last_error=True)
_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_STARTF_USESTDHANDLES = 0x100
_DUPLICATE_SAME_ACCESS = 0x2
_INFINITE = 0xFFFFFFFF
_STD_HANDLES = (-10, -11, -12)  # input, output, error
_WM_CLOSE = 0x10
_GWL_STYLE = -16
_GW_OWNER = 4
_WS_SYSMENU = 0x80000  # the window has a close button


class _StartupInfo(ctypes.Structure):
    _fields_ = (
        ("cb", wintypes.DWORD),
        ("lpReserved", wintypes.LPWSTR),
        ("lpDesktop", wintypes.LPWSTR),
        ("lpTitle", wintypes.LPWSTR),
        ("dwX", wintypes.DWORD),
        ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD),
        ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD),
        ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD),
        ("cbReserved2", wintypes.WORD),
        ("lpReserved2", ctypes.c_void_p),
        ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE),
        ("hStdError", wintypes.HANDLE),
    )


class _ProcessInformation(ctypes.Structure):
    _fields_ = (("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE), ("dwProcessId", wintypes.DWORD), ("dwThreadId", wintypes.DWORD))


_user32.InternalGetWindowText.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
_user32.IsWindowVisible.argtypes = (wintypes.HWND,)
_user32.GetWindowLongPtrW.argtypes = (wintypes.HWND, ctypes.c_int)
_user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
_user32.GetWindow.argtypes = (wintypes.HWND, wintypes.UINT)
_user32.GetWindow.restype = wintypes.HWND
_user32.SetThreadDesktop.argtypes = (wintypes.HANDLE,)
_user32.PostMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
_kernel32.CreateProcessW.argtypes = (
    wintypes.LPCWSTR,
    wintypes.LPWSTR,
    ctypes.c_void_p,
    ctypes.c_void_p,
    wintypes.BOOL,
    wintypes.DWORD,
    ctypes.c_void_p,
    wintypes.LPCWSTR,
    ctypes.POINTER(_StartupInfo),
    ctypes.POINTER(_ProcessInformation),
)
_kernel32.GetStdHandle.argtypes = (wintypes.DWORD,)
_kernel32.GetStdHandle.restype = wintypes.HANDLE
_kernel32.GetCurrentProcess.restype = wintypes.HANDLE
_kernel32.DuplicateHandle.argtypes = (
    wintypes.HANDLE,
    wintypes.HANDLE,
    wintypes.HANDLE,
    ctypes.POINTER(wintypes.HANDLE),
    wintypes.DWORD,
    wintypes.BOOL,
    wintypes.DWORD,
)
_kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
_kernel32.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
_kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


def _open_desktop() -> int:
    """a handle to the hidden desktop, made when there's none yet."""
    handle = windows.open_hidden_desktop(create=True)
    if not handle:
        raise ToolError(f"no desktop {DESKTOP}: {ctypes.WinError(ctypes.get_last_error())}")
    return handle


def _inheritable(handle: int | None) -> int | None:
    """an inheritable copy of one of this process's standard handles (None when it has none)."""
    if not handle or handle == windows.INVALID_HANDLE:
        return None
    copy = wintypes.HANDLE()
    process = _kernel32.GetCurrentProcess()
    if not _kernel32.DuplicateHandle(process, handle, process, ctypes.byref(copy), 0, True, _DUPLICATE_SAME_ACCESS):
        return None
    return copy.value


def run(command: list[str]) -> int:
    """runs a command on the hidden desktop with this process's standard handles and waits for it."""
    desktop = _open_desktop()  # held until the command ends: the desktop goes with its last handle
    std = [_inheritable(_kernel32.GetStdHandle(which)) for which in _STD_HANDLES]
    try:
        _kernel32.SetEnvironmentVariableW(windows.HIDDEN_DESKTOP_ENV, DESKTOP)
        startup = _StartupInfo(cb=ctypes.sizeof(_StartupInfo), lpDesktop=DESKTOP, dwFlags=_STARTF_USESTDHANDLES)
        startup.hStdInput, startup.hStdOutput, startup.hStdError = std
        info = _ProcessInformation()
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
        if not _kernel32.CreateProcessW(None, line, None, None, True, 0, None, None, ctypes.byref(startup), ctypes.byref(info)):
            raise ToolError(f"{command[0]} didn't start: {ctypes.WinError(ctypes.get_last_error())}")
        _kernel32.CloseHandle(info.hThread)
        _kernel32.WaitForSingleObject(info.hProcess, _INFINITE)
        code = wintypes.DWORD()
        _kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code))
        _kernel32.CloseHandle(info.hProcess)
        return int(code.value)
    finally:
        for handle in std:
            if handle:
                _kernel32.CloseHandle(handle)
        windows.close_desktop(desktop)


windows_on_desktop = windows.windows_on_hidden_desktop


def _move_onto_desktop() -> None:
    """this thread onto the hidden desktop (messages and window texts don't cross desktops). the handle stays open: the
    thread uses it, and it goes with the process."""
    desktop = windows.open_hidden_desktop(create=False)
    if not desktop:
        raise ToolError(f"no {DESKTOP} desktop: nothing runs there")
    if not _user32.SetThreadDesktop(desktop):
        raise ToolError(f"can't reach {DESKTOP}: {ctypes.WinError(ctypes.get_last_error())}")


def _title(window: int) -> str:
    text = ctypes.create_unicode_buffer(256)
    _user32.InternalGetWindowText(window, text, len(text))  # sends no message: a hung window answers too
    return text.value


def list_processes() -> None:
    """prints each process with a window on the hidden desktop: its pid, exe and visible windows' titles."""
    if not windows_on_desktop():
        print(f"nothing runs on {DESKTOP}")
        return
    _move_onto_desktop()
    found: dict[int, list[str]] = {}
    for window, pid in windows_on_desktop():
        titles = found.setdefault(pid, [])
        if _user32.IsWindowVisible(window) and (title := _title(window)):
            titles.append(title)
    for pid, titles in sorted(found.items()):
        shown = ", ".join(f"'{title}'" for title in titles) or "no visible window with a title"
        print(f"{pid} {windows.exe_path(pid) or '?'}: {shown}")


def close(pid: int) -> None:
    """posts WM_CLOSE to the process's windows there with a close button that no other window owns: its main ones
    (minecraft's others are hidden utility, tooltip and ime windows)."""
    _move_onto_desktop()
    asked = 0
    for window, owner in windows_on_desktop():
        main = not _user32.GetWindow(window, _GW_OWNER) and _user32.GetWindowLongPtrW(window, _GWL_STYLE) & _WS_SYSMENU
        if owner == pid and main:
            if not _user32.PostMessageW(window, _WM_CLOSE, 0, 0):
                raise ToolError(f"{pid} wasn't asked to close: {ctypes.WinError(ctypes.get_last_error())}")
            asked += 1
    if not asked:
        raise ToolError(f"{pid} has no window with a close button on {DESKTOP}")
    print(f"asked {pid} to close")


def main() -> int:
    parser = argparse.ArgumentParser(description="run a command on a hidden desktop of its own")
    parser.add_argument("--list", action="store_true", help="the processes with a window there, with their exes and window titles")
    parser.add_argument("--close", type=int, metavar="PID", help="ask a process there to close")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    if options.list:
        list_processes()
        return 0
    if options.close:
        close(options.close)
        return 0
    if not options.command:
        raise ToolError("no command (or --list, --close)")
    return run(options.command)


if __name__ == "__main__":
    raise SystemExit(main())
