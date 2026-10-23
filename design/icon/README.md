# zoot launcher icon

`zoot-icon.svg` is the canonical vector source. The original emblem uses a
broken ring, a square core, and two linked terminal nodes, reflecting ZOOT's
command-system role. It does not reproduce official game artwork.

Run `python3 design/icon/render_icons.py` to update the Android foreground and
monochrome vectors, the five legacy launcher PNG densities, and `preview.png`.
The renderer requires `rsvg-convert` and Pillow. Android applies the user's
launcher mask and themed-icon colors to the adaptive and monochrome versions.

The SVG uses Android's 108dp adaptive-icon viewport. The legacy PNGs use the
same central 72dp visible region, so their emblem matches the adaptive icon's
size. The artwork remains inside the adaptive safe circle.
