@echo off
rem Compila o GuitarraNoPC.exe no Windows.
rem Precisa do zig (https://ziglang.org/download) e do git instalados e no PATH.
setlocal
cd /d "%~dp0"
set R=third_party\raylib
if not exist %R% git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git %R% || exit /b 1
if not exist build\obj mkdir build\obj
set CC=zig cc -target x86_64-windows-gnu -O2
set OBJS=
for %%f in (rcore rshapes rtextures rtext utils rglfw rmodels) do (
    %CC% -c %R%\src\%%f.c -o build\obj\%%f.o -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 -I%R%\src -I%R%\src\external\glfw\include -w || exit /b 1
)
for %%f in (audio dsp rigio recorder) do (
    %CC% -c src\%%f.c -o build\obj\%%f.o -Ithird_party -w || exit /b 1
)
%CC% -c src\gui.c -o build\obj\gui.o -I%R%\src -w || exit /b 1
pushd icon
zig rc /fo ..\build\app.res app.rc || (popd & exit /b 1)
popd
%CC% -s build\obj\rcore.o build\obj\rshapes.o build\obj\rtextures.o build\obj\rtext.o build\obj\utils.o build\obj\rglfw.o build\obj\rmodels.o build\obj\audio.o build\obj\dsp.o build\obj\rigio.o build\obj\recorder.o build\obj\gui.o build\app.res -o build\GuitarraNoPC.exe -lopengl32 -lgdi32 -lwinmm -lole32 -lshell32 -luser32 -Wl,--subsystem,windows || exit /b 1
echo Pronto: build\GuitarraNoPC.exe
