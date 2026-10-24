"""Gera src/icon_data.h (ícone da janela) a partir dos PNGs de tools/icone_gerado.
Uso: python tools/make_icon.py && python tools/gen_icon_data.py"""
import os
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
with open(os.path.join(root, "src", "icon_data.h"), "w") as f:
    for s in (16, 32, 48, 64, 256):
        b = open(os.path.join(here, "icone_gerado", f"icon_{s}.png"), "rb").read()
        f.write(f"static const unsigned char icon_png_{s}[{len(b)}] = {{" + ",".join(map(str, b)) + "};\n")
print("src/icon_data.h gerado")
