@echo off
rem Build with MSVC. Run from a "x64 Native Tools Command Prompt for VS".
rem /STACK is raised because the renderer and physics world are large flat
rem structs that a frame's worth of instances lives inside.
if not exist build mkdir build
cl /nologo /std:c++14 /EHsc /O2 /W3 /Fe:build\pix.exe /Fo:build\ src\main.cpp /link /STACK:67108864
