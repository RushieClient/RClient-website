# RClient slate

`rclient_slate.png` — original, programmatically drawn terrain and decorations.
No textures from other maps are used. `generate.py` is the editable source;
run it with Python and Pillow to regenerate the atlas.

PNG: 1024×1024 RGBA, 16×16 cells, 64 pixels per cell. Tile 0 is transparent.
Tile indices below start at zero, counting left to right, top to bottom.

| Purpose | Indices |
| --- | --- |
| Terrain top: left corner, middle, right corner | 1, 2, 3 |
| Terrain middle: left edge, fill, right edge | 17, 18, 19 |
| Terrain bottom: left corner, middle, right corner | 33, 34, 35 |
| One-tile-high platform: left, middle, right | 5, 6, 7 |
| One-tile-wide column: top, middle, bottom | 9, 25, 41 |
| Single block | 11 |
| Interior variations | 49–56 |
| Quiet rock interiors | 57–60 |
| Transparent freeze overlay | 64 |
| No-hook surface | 65 |
| Grass | 81–84 |
| Flowers | 97–100 |
| Hanging roots | 113–116 |
| Small stones | 129–132 |

In the DDNet editor, add the PNG in Images, mark it as embedded, and assign
it to a decorative Tiles layer. Collision is painted separately on Game.
There is no automapper configuration. The map is not modified by this pack.

`concept.png` is the earlier imagegen art concept, NOT a grid-ready atlas.
Its generation prompt: original dark-blue slate terrain, subtle cyan rims,
moss, roots and pale-blue flowers; transparent background, 2D side view,
no existing game textures, no text or logos. Built-in imagegen was used.
