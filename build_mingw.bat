@echo off
REM Build with MinGW-w64 (g++ in PATH).
g++ main.cpp -o macromaker.exe -O2 -mwindows -static -luser32 -lgdi32
