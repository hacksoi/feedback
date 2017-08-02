@echo off

pushd ..\build
cl /nologo /Zi /W4 /wd4100 /wd4201 /wd4505 /wd4189 /wd4101 /wd4005 /wd4996 /I..\src\glew ..\src\feedback.cpp ..\src\imgui\*.obj /link /LIBPATH:..\src\ glew32.lib User32.lib Kernel32.lib Gdi32.lib OpenGL32.lib
popd
