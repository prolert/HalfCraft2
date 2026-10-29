@echo off
rem HalfCraft in Russian: starts HalfCraft.exe beside this file, which then speaks Russian whatever
rem windows' language (HALFCRAFT_LANG), on half-life 2: deathmatch's engine (--engine hl2dm; without
rem deathmatch it offers steam's install of it, or plays on half-life 2's own). arguments go on to
rem Half-Life 2, as HalfCraft.exe's do; an --engine hl2 among them wins over this one. ascii only: cmd
rem misreads a batch file's other bytes under some code pages.
setlocal
set "HALFCRAFT_LANG=ru"
start "" "%~dp0HalfCraft.exe" --engine hl2dm %*
