@echo off

SET LIB_INCLUDE_PATHS=/I..\libraries\glew\include /I..\libraries\imgui\include /I..\libraries\nps_utils\include
SET LIBPATHS=/LIBPATH:..\libraries\glew\bin
REM SET LIB_SRC_PATHS=..\libraries\imgui\src\*.cpp
SET LIB_SRC_PATHS=.\imgui*.obj

set COMMON_COMPILER_FLAGS=/nologo /Oi /Z7 /W4 /wd4100 /wd4201 /wd4505 /wd4189 /wd4101 /wd4996 %LIB_INCLUDE_PATHS%
set COMMON_LINKER_FLAGS=/opt:ref /incremental:no %LIBPATHS%

pushd ..\build

del *.pdb > NUL 2> NUL
echo WAITING FOR PDB > lock.tmp
cl %COMMON_COMPILER_FLAGS% /LD ..\src\feedback.cpp %LIB_SRC_PATHS% /link %COMMON_LINKER_FLAGS% /PDB:feedback_%random%.pdb /EXPORT:AppInitialize /EXPORT:AppUpdateAndRender glew32.lib OpenGL32.lib
del lock.tmp
cl %COMMON_COMPILER_FLAGS% ..\src\win32_feedback.cpp %LIB_SRC_PATHS% /link %COMMON_LINKER_FLAGS% glew32.lib User32.lib Kernel32.lib Gdi32.lib OpenGL32.lib Winmm.lib

popd
