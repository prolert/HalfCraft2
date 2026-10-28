"""what windows knows about other processes and the files and mutexes they hold, through its own api."""

import ctypes
import msvcrt
import os
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_user32 = ctypes.WinDLL("user32", use_last_error=True)
_DESKTOP_ALL_ACCESS = 0x10000000  # GENERIC_ALL
_TH32CS_SNAPPROCESS = 0x2
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_SYNCHRONIZE = 0x00100000
_GENERIC_READ = 0x80000000
_FILE_SHARE_ALL = 0x1 | 0x2 | 0x4  # read, write, delete
_OPEN_EXISTING = 3
_ERROR_FILE_NOT_FOUND = 2
_ERROR_PATH_NOT_FOUND = 3
_ERROR_SHARING_VIOLATION = 32
_ERROR_LOCK_VIOLATION = 33
INVALID_HANDLE = ctypes.c_void_p(-1).value

# tools/game/hidden_desktop.py's desktop, and the variable it sets for what it runs (gradle runs without a daemon then)
HIDDEN_DESKTOP = "HalfCraftTest"
HIDDEN_DESKTOP_ENV = "HALFCRAFT_DESKTOP"


class _ProcessEntry(ctypes.Structure):
    _fields_ = (
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    )


_kernel32.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
_kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
_kernel32.Process32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.Process32NextW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_kernel32.OpenProcess.restype = wintypes.HANDLE
_FILETIME_P = ctypes.POINTER(wintypes.FILETIME)
_kernel32.GetProcessTimes.argtypes = (wintypes.HANDLE, _FILETIME_P, _FILETIME_P, _FILETIME_P, _FILETIME_P)
_kernel32.GetSystemTimeAsFileTime.argtypes = (_FILETIME_P,)
_kernel32.OpenMutexW.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR)
_kernel32.OpenMutexW.restype = wintypes.HANDLE
_kernel32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
_kernel32.CreateFileW.restype = wintypes.HANDLE
_kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
_user32.OpenDesktopW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_user32.OpenDesktopW.restype = wintypes.HANDLE
_user32.CreateDesktopW.argtypes = (wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p)
_user32.CreateDesktopW.restype = wintypes.HANDLE
_user32.CloseDesktop.argtypes = (wintypes.HANDLE,)
_user32.EnumDesktopWindows.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.LPARAM)
_user32.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
_ENUM_WINDOWS = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
_kernel32.QueryFullProcessImageNameW.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD))


@dataclass(frozen=True)
class Process:
    pid: int
    name: str  # its exe's file name, e.g. hl2.exe
    started: int | None  # when, in windows' 100 ns ticks; None when windows won't say


def _started(pid: int) -> int | None:
    handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        created, ended, kernel, user = (wintypes.FILETIME() for _ in range(4))
        if not _kernel32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel), ctypes.byref(user)):
            return None
        return created.dwHighDateTime << 32 | created.dwLowDateTime
    finally:
        _kernel32.CloseHandle(handle)


def now() -> int:
    """the time in windows' 100 ns ticks, the unit of Process.started (both utc)."""
    ticks = wintypes.FILETIME()
    _kernel32.GetSystemTimeAsFileTime(ctypes.byref(ticks))
    return ticks.dwHighDateTime << 32 | ticks.dwLowDateTime


def exe_path(pid: int) -> str | None:
    """the full path of a process's exe, None when windows won't say."""
    handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        size = wintypes.DWORD(1024)
        path = ctypes.create_unicode_buffer(size.value)
        return path.value if _kernel32.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)) else None
    finally:
        _kernel32.CloseHandle(handle)


def runs_exe(pid: int, exe: Path) -> bool:
    """whether a process runs this very exe (not just one of its name): the paths resolved, case aside. False when windows won't give its path."""
    path = exe_path(pid)
    return path is not None and str(Path(path).resolve()).lower() == str(exe.resolve()).lower()


def open_hidden_desktop(*, create: bool) -> int | None:
    """a handle to HIDDEN_DESKTOP (CloseDesktop it), made first when create says so; None when it isn't there."""
    handle = _user32.OpenDesktopW(HIDDEN_DESKTOP, 0, False, _DESKTOP_ALL_ACCESS)
    if not handle and create:
        handle = _user32.CreateDesktopW(HIDDEN_DESKTOP, None, None, 0, _DESKTOP_ALL_ACCESS, None)
    return handle or None


def close_desktop(handle: int) -> None:
    _user32.CloseDesktop(handle)


def windows_on_hidden_desktop() -> list[tuple[int, int]]:
    """the top-level windows on HIDDEN_DESKTOP with their processes' pids, front to back (none when it isn't there)."""
    desktop = open_hidden_desktop(create=False)
    if not desktop:
        return []
    found: list[tuple[int, int]] = []

    def each(window: int, _: int) -> bool:
        pid = wintypes.DWORD()
        _user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
        if pid.value != os.getpid():
            found.append((window, pid.value))
        return True

    try:
        _user32.EnumDesktopWindows(desktop, _ENUM_WINDOWS(each), 0)
    finally:
        _user32.CloseDesktop(desktop)
    return found


def processes(*names: str) -> list[Process]:
    """the running processes whose exe has one of these names (any case), oldest first."""
    wanted = {name.lower() for name in names}
    snapshot = _kernel32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snapshot == INVALID_HANDLE:
        raise ctypes.WinError(ctypes.get_last_error())
    found = []
    try:
        entry = _ProcessEntry(dwSize=ctypes.sizeof(_ProcessEntry))
        more = _kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            if entry.szExeFile.lower() in wanted:
                found.append(Process(entry.th32ProcessID, entry.szExeFile, _started(entry.th32ProcessID)))
            more = _kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        _kernel32.CloseHandle(snapshot)
    return sorted(found, key=lambda process: (process.started is None, process.started or 0))


def mutex_exists(name: str) -> bool:
    """whether some process holds a named mutex (e.g. Local\\HalfCraft_v1_minecraft)."""
    handle = _kernel32.OpenMutexW(_SYNCHRONIZE, False, name)
    if handle:
        _kernel32.CloseHandle(handle)
        return True
    return ctypes.get_last_error() != _ERROR_FILE_NOT_FOUND  # there, but not ours to open


def _open(path: Path, share: int) -> int | None:
    """a handle to an existing file, None when it isn't there; OSError when windows won't open it."""
    handle = _kernel32.CreateFileW(str(path), _GENERIC_READ, share, None, _OPEN_EXISTING, 0, None)
    if handle == INVALID_HANDLE:
        error = ctypes.get_last_error()
        if error in (_ERROR_FILE_NOT_FOUND, _ERROR_PATH_NOT_FOUND):
            return None
        raise ctypes.WinError(error)
    return handle


def in_use(path: Path) -> bool:
    """whether another process has the file open in a way that keeps others out (a writer, mostly)."""
    try:
        handle = _open(path, 0)
    except OSError as error:
        if error.winerror in (_ERROR_SHARING_VIOLATION, _ERROR_LOCK_VIOLATION):
            return True
        raise
    if handle is not None:
        _kernel32.CloseHandle(handle)
    return False


def open_shared(path: Path) -> BinaryIO | None:
    """a file opened for reading that its writer can still write, rename or delete (a game's log), or None."""
    handle = _open(path, _FILE_SHARE_ALL)
    if handle is None:
        return None
    return os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY), "rb")
