#!/usr/bin/env sh
# Compila o GuitarraNoPC.exe (Windows 64 bits) usando o zig.
# Funciona no Linux, no macOS ou no WSL. Precisa de git e zig.
# Sem o zig instalado:  pip install ziglang   e rode:  ZIG="python3 -m ziglang" ./build.sh
set -e
cd "$(dirname "$0")"
ZIG="${ZIG:-zig}"
RAYLIB=third_party/raylib
[ -d "$RAYLIB" ] || git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git "$RAYLIB"
mkdir -p build/obj
CC="$ZIG cc -target x86_64-windows-gnu -O2"
OBJS=""
for f in rcore rshapes rtextures rtext utils rglfw rmodels; do
    $CC -c "$RAYLIB/src/$f.c" -o "build/obj/$f.o" -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 \
        -I"$RAYLIB/src" -I"$RAYLIB/src/external/glfw/include" -w
    OBJS="$OBJS build/obj/$f.o"
done
for f in audio dsp rigio recorder; do
    $CC -c "src/$f.c" -o "build/obj/$f.o" -Ithird_party -w
    OBJS="$OBJS build/obj/$f.o"
done
$CC -c src/gui.c -o build/obj/gui.o -I"$RAYLIB/src" -w
OBJS="$OBJS build/obj/gui.o"
(cd icon && $ZIG rc /fo ../build/app.res app.rc)
$CC -s $OBJS build/app.res -o build/GuitarraNoPC.exe \
    -lopengl32 -lgdi32 -lwinmm -lole32 -lshell32 -luser32 -Wl,--subsystem,windows
echo "Pronto: build/GuitarraNoPC.exe"
