"""Gera src/fonts.h com as fontes embutidas no programa.
Uso: python tools/gen_fonts.py"""
import os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fonts = [("font_ui", "Barlow-Medium.ttf"), ("font_bold", "BarlowCondensed-SemiBold.ttf"), ("font_script", "Yellowtail-Regular.ttf")]
with open(os.path.join(root, "src", "fonts.h"), "w") as f:
    for name, file in fonts:
        b = open(os.path.join(root, "fonts", file), "rb").read()
        f.write(f"static const unsigned char {name}[{len(b)}] = {{" + ",".join(str(x) for x in b) + "};\n")
print("src/fonts.h gerado")
