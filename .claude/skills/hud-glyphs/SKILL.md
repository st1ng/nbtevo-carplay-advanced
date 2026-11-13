---
name: hud-glyphs
description: Generate, change, add or check the HUD maneuver pictures (src/hudglyphs.h from Mapbox's ManeuversStyleKit.swift via tools/hud_glyphs.py, and the procedural shapes in src/hudshapes.h: roundabouts, lanes, digits, the MINI mapping). Use for anything about how a turn arrow looks on the cluster/HUD, its piece count, or regenerating the glyphs.
---

# HUD maneuver pictures

The cluster's HUDTerminal draws only **filled convex polygons** (`AddPolygon`, type 3, 3..8 points), one MOST call each (about 20 ms
apiece in the car), into the 6WA picture box (70,20)-(170,120), y down. Every picture is therefore a list of convex pieces with a
tone: 0 = route (white), 1 = context (grey), 2 = effect.

| Part | File | Made by |
|---|---|---|
| Mapbox's static drawings (12 glyphs + the roundabout's exit arrow) | `src/hudglyphs.h` | **generated** by `tools/hud_glyphs.py`: never edit by hand, never clang-format |
| Drawing interpreter | `tools/mapbox_stylekit.py` | interprets the Swift drawing code into shapes |
| Everything per maneuver | `src/hudshapes.h` | hand-written C++: type -> glyph, mirroring, the roundabout, junction roads, exit digits, lanes, the MINI mapping |
| Who sends them | `src/hud.h` | draws `shapeOf(...)` into the hidden buffer and swaps |

## Regenerate `hudglyphs.h`

```bash
curl -sL https://raw.githubusercontent.com/mapbox/mapbox-navigation-ios/v0.40.0/MapboxNavigation/ManeuversStyleKit.swift -o /tmp/msk.swift
curl -sL https://raw.githubusercontent.com/mapbox/mapbox-navigation-ios/v0.40.0/MapboxNavigation/LanesStyleKit.swift -o /tmp/lsk.swift
python3 tools/hud_glyphs.py /tmp/msk.swift /tmp/lsk.swift src/hudglyphs.h /tmp/glyphs.json      # the JSON: an optional preview
MAPBOX_STYLEKIT=/tmp/msk.swift python3 -m unittest tests.test_hud_shapes           # test_glyphs_generated: the file is what the tool makes
```

The source is pinned to **mapbox-navigation-ios v0.40.0** and is downloaded, not kept in this repo. With the unchanged tool the output
is byte-identical to the committed file: `12 glyphs, 180 pieces; roundabout arrow 6 pieces`.

## How the generator works (`tools/hud_glyphs.py`)

1. `mapbox_stylekit.functions()` splits the Swift file into its `draw<Name>` functions, and `run()` interprets the PaintCode drawing
   code into absolute shapes on Mapbox's 32 x 32 canvas: affine transforms, `UIBezierPath` moves/lines/curves, fills, strokes with
   width and dash, primary/secondary colours.
2. Curves are flattened (`cubic`, tolerance `TOL` = 0.08 Mapbox units), and outlines are simplified (Douglas-Peucker, `EPS` = 0.10).
3. **Fills** are cut into triangles (ear clipping) and merged back into convex pieces of at most 8 points (Hertel-Mehlhorn). A fill
   with a hole (the destination pin) gets the hole drawn on top in tone 1.
4. **Strokes** become one quad per segment, overlapping a little so the joins close.
5. Points are scaled into the box (`BOX` = 100 / 32: `x' = 70 + x * BOX`, `y' = 20 + y * BOX`) and rounded.
6. `STATIC` lists the glyphs written as `kName[]` + `kNameN`: Arrowstraight, Arrowright, Arrowslightright, Arrowsharpright,
   Arrow180right, Arrive, Arriveright, Starting, Destination, Merge, Fork, Offramp. `kRbArrow` is the roundabout's exit arrow
   (`drawRoundabout`'s `bezier2Path`) in its own frame, in 1/16 units; the head unit turns it to the exit angle.

`struct Piece { unsigned char tone, n; short p[8][2]; }`: tone 0 is the route, tone 1 the context; points are in the 6WA box.

## How the pictures are built (`src/hudshapes.h`)

- `shapeOf(type, angle, side, jtype, angles, nangles)` -> `Shape` (up to `kMaxPieces` = 96 pieces): the iAP2 maneuver type 0..53
  (CPManeuverType) picks a glyph or a procedural drawing.
  - Glyphs are added with `addGlyph(s, HUD_GLYPH(Name), mirrored)`. **Left turns are right turns mirrored** (`x -> 240 - x`),
    because Mapbox only draws the right-hand versions.
  - Left-hand traffic (`side` 1) mirrors the picture (roundabouts clockwise, U-turns to the right). Digits are never mirrored.
- The **roundabout** is Mapbox's parametric `drawRoundabout` computed for any exit: the ring in the context tone, the way round and
  the exit arrow in the route tone, the composition shifted away from the arrow. Thin roads mark the exits passed (exits 2..4), or the
  junction's real roads when the iPhone sends `jtype` + angles. The exit number is drawn from exit 5 on, in seven-segment digits.
  Arcs are cut into pieces of at most 40 degrees, so each piece stays convex.
- **Lanes** (`addLanes`): the picture is shrunk to 11/16 at the top of the box, and a strip of lane arrows goes under it (y 94..118).
  Lanes to take are in the route tone with their way out highlighted; the others are dim. Lanes stay where they are on the road (not
  mirrored for left-hand traffic).
- **The MINI cluster** shows a smaller area: `hud.h` maps every point to (35,15)-(85,65) with `toMini` (in `hudshapes.h`), and pieces
  that fold flat are dropped.
- **No floating point and no division by a variable** (the car's libc has no soft-float, and `udiv` is used for division): angles are
  in degrees with an integer sine table (`isin`/`icos`, scale 1024), and Mapbox's canvas is in 1/16 units (`boxX`/`boxY`).

## Look at them

```bash
python3 .claude/skills/hud-glyphs/preview.py /tmp/p.svg --glyphs                      # every generated glyph, as stored
python3 .claude/skills/hud-glyphs/preview.py /tmp/p.svg 1 2 49 4 30:0:0 6:120:1 12     # pictures as the head unit builds them
python3 .claude/skills/hud-glyphs/preview.py /tmp/p.svg 33:0:0:j1:-90,0,90 --lanes "0/1000/-90;1/1000/0;2/90/0,90" --mini
rsvg-convert /tmp/p.svg -o /tmp/p.png                                                   # if a PNG is easier to view
```

A picture is `TYPE[:ANGLE[:SIDE[:jJTYPE:A,A,...]]]`. The renderer compiles `src/hudshapes.h` with the same harness as
`tests/test_hud_shapes.py`, so it shows exactly what the car is sent. Each cell lists its piece count: watch it, because every piece is
a MOST call.

## Changing things

- **A different look for an existing glyph** (tolerances, stroke overlap, merging): change `tools/hud_glyphs.py`, regenerate, look
  at the result with `preview.py --glyphs`, and check the piece counts in the summary. `test_glyphs_generated` compares against the
  new file by itself; update other tests only if a rule changed.
- **A new glyph from Mapbox**: add its `draw<Name>` name (without `draw`) to `STATIC`, regenerate, then use `HUD_GLYPH(Name)` in
  `shapeOf` for the maneuver types that should show it.
- **A new procedural detail**: write it in `hudshapes.h` with `add`, `quad16`, `rect16`, `arc` or `road`. Keep every piece convex with
  3..8 points and inside the box, and stay within `kMaxPieces`.
- Always run `python3 -m unittest tests.test_hud_shapes`. It builds every type and many angles and sides, and checks that pieces are
  convex and inside the box, the roundabout details, the mirroring and the lanes.
- In the car, a shape can be tried without a rebuild through the HUD test channel: write lines of `"<fkt hex> <payload hex>"` (e.g.
  `21d` AddPolygon calls) to `/dev/shmem/carplaymenu-hudcmd` while "CarPlay directions in HUD" is on and no route is active. The
  answers go to `/dev/shmem/carplaymenu.log`.

## Licence

- `tools/` is our own code (it contains no Mapbox code) and is licensed like the rest of the repo.
- `ManeuversStyleKit.swift` (mapbox-navigation-ios v0.40.0) is ISC: use, copy, modify and distribute for any purpose, provided the
  copyright and permission notice go with all copies. The drawings come from Mapbox's CC0 `Maneuvers.pcvd`. The file is downloaded
  at its pinned tag, not kept in this repo.
- `hudglyphs.h` is a derived work: the generator writes Mapbox's copyright and permission notice into its header. Keep that notice.
  `NOTICE` names the file as separately licensed.
