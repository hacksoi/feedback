@echo off

set COMMON_COMPILER_FLAGS=/nologo /Oi /Z7 /W4 /wd4100 /wd4201 /wd4505 /wd4189 /wd4101 /wd4996 /I..\src\glew 
set COMMON_LINKER_FLAGS=/opt:ref /incremental:no

pushd ..\build

REM cl /c %COMMON_COMPILER_FLAGS% ..\src\imgui\*.cpp /link %COMMON_LINKER_FLAGS% /PDB:imgui.pdb

del *.pdb > NUL 2> NUL
echo WAITING FOR PDB > lock.tmp
cl %COMMON_COMPILER_FLAGS% /LD ..\src\feedback.cpp .\imgui*.obj /link %COMMON_LINKER_FLAGS% /PDB:feedback_%random%.pdb /EXPORT:AppInitialize /EXPORT:AppReload /EXPORT:AppTouchDown /EXPORT:AppTouchUp /EXPORT:AppTouchMovement /EXPORT:AppNonTouchMovement /EXPORT:AppZoomIn /EXPORT:AppZoomOut /EXPORT:AppKeyDown /EXPORT:AppKeyUp /EXPORT:AppRender glew32.lib OpenGL32.lib
del lock.tmp
cl %COMMON_COMPILER_FLAGS% ..\src\win32_feedback.cpp .\imgui*.obj /link /MAP %COMMON_LINKER_FLAGS% glew32.lib User32.lib Kernel32.lib Gdi32.lib OpenGL32.lib

popd
