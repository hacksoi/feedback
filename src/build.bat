@echo off

pushd ..\build
cl /nologo /Zi /I..\..\libraries\glew\include /W4 /wd4100 /wd4201 /wd4505 /wd4189 /wd4101 /wd4005 /wd4996 ..\src\feedback.cpp /link /LIBPATH:..\..\libraries\glew\lib\Release\x64 glew32.lib User32.lib Kernel32.lib Gdi32.lib OpenGL32.lib
popd
