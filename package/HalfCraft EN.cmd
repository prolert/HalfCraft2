@echo off
rem HalfCraft in English: starts HalfCraft.exe beside this file, which then speaks English whatever
rem windows' language (HALFCRAFT_LANG). arguments go on to Half-Life 2, as HalfCraft.exe's do. ascii only:
rem cmd misreads a batch file's other bytes under some code pages.
setlocal
set "HALFCRAFT_LANG=en"
start "" "%~dp0HalfCraft.exe" %*
