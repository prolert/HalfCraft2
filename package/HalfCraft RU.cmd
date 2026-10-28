@echo off
rem HalfCraft in Russian: starts HalfCraft.exe beside this file, which then speaks Russian whatever
rem windows' language (HALFCRAFT_LANG). arguments go on to Half-Life 2, as HalfCraft.exe's do. ascii only:
rem cmd misreads a batch file's other bytes under some code pages.
setlocal
set "HALFCRAFT_LANG=ru"
start "" "%~dp0HalfCraft.exe" %*
