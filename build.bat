@echo off
REM Build with MSVC. Run from a "x64 Native Tools Command Prompt for VS".
cl /nologo /O2 /EHsc main.cpp /link user32.lib gdi32.lib /SUBSYSTEM:WINDOWS /OUT:macromaker.exe
