# AGENTS.md

Notes for coding agents working on TitanVSP. Read this before changing anything. For what the
fork adds, see [CHANGES.md](CHANGES.md). For building and using it, see [README.md](README.md).

## What this repository is

TitanVSP is OpenVSP 3.55.0 (NASA's parametric aircraft geometry tool, C++ with FLTK and OpenGL)
plus a small set of changes. The first commit imports upstream OpenVSP 3.55.0 as one squashed
commit; everything after it is TitanVSP's. To see exactly what the fork changes:

```sh
git diff $(git rev-list --max-parents=0 HEAD) HEAD --stat
```

Everything outside those files is upstream code. Leave it as it is unless a task needs a change
there: smaller diffs keep upstream updates easy (see [Upstream updates](#upstream-updates)).

## Layout

| Path | What's there |
| --- | --- |
| `src/geom_core/` | The model: `Vehicle`, `Geom` and every component type, Parms, XML I/O. **`FilletGeom.cpp/.h` is TitanVSP's.** |
| `src/gui_and_draw/` | FLTK screens. `ScreenMgr.cpp` holds the dark theme and the file chooser; `ManageGeomScreen.cpp` registers each component's screen. **`FilletScreen.cpp/.h` is TitanVSP's.** |
| `src/vsp_graphic/` | OpenGL rendering: `Camera`, `Viewport`, `Background`, picking. |
| `src/geom_api/` | Public API (`APIDefines.h` enums, `VSP_Geom_API`); SWIG makes the Python bindings from it. |
| `src/vsp/main.h.in` | Version strings (the TitanVSP name). |
| `dev/` | Windows build, run and shortcut scripts (TitanVSP's). |
| `docs/images/` | README screenshots. |
| `Libraries/` | Third-party library superbuild (upstream). |

## Build, run and test

Windows is the only platform TitanVSP is built and tested on.

```powershell
powershell -ExecutionPolicy Bypass -File dev\build.ps1   # incremental build + install
powershell -ExecutionPolicy Bypass -File dev\run.ps1     # build, then launch vsp.exe
```

- The build lives **outside** the repo, in `..\OpenVSP-build\`: `Libraries` (built once), `vsp`
  (the CMake tree), `install` (the runnable `vsp.exe`), and `vsp\venv` (Python with `openvsp`).
- The freshly linked exe is `..\OpenVSP-build\vsp\vsp\Release\vsp.exe`. The install step copies
  it to `install\`.
- **`file INSTALL cannot copy file`** means `vsp.exe` is running and locked. Everything still
  compiled. Don't kill the user's running app: ask them to close it, or test with the Release
  exe.
- The release zip comes from
  `cmake --build ..\OpenVSP-build\vsp --config Release --target PACKAGE`, which writes
  `..\OpenVSP-build\vsp\TitanVSP-3.55.0-win64.zip`.

**Testing.** OpenVSP's test suites aren't wired into the dev build. Changes are checked by running
them:

- **Geometry:** script the Python API (`..\OpenVSP-build\vsp\venv\Scripts\python.exe`), e.g.
  build a model, `Update()`, and export STL for the fillet. Check STL exports for watertightness
  (no open, non-manifold or degenerate edges) whenever the fillet changes.
- **GUI:** launch `vsp.exe` with a `.vsp3` and look at it, or take screenshots of its windows.
  The Python facade (`openvsp_config.LOAD_FACADE = True`, then `vsp.StartGUI()`) can open
  screens from a script. It parses `sys.argv`, so pass a script's own arguments through
  environment variables.
- **Upstream code:** before saying a change is safe, check the other screens or callers that
  share the code.

## Conventions

- **Code style:** match upstream. Four-space indents, spaces inside parentheses
  (`foo( a, b )`), braces on their own lines, `m_` members, `//==== Section ====//` headers.
- **Comments:** TitanVSP's code comments explain *why* in plain sentences, as in
  `FilletGeom.cpp`. Keep that density and tone in new code.
- **Line endings:** files are checked out with CRLF (`core.autocrlf`, `* text=auto`). Tools that
  rewrite files must keep each file's existing line endings. On Windows, Python's text mode
  writes CRLF, so read and write bytes.
- **New enum values:** add them at the end, just before `NUM_...` (as `FILLET_GEOM_TYPE` and
  `FILLET_GEOM_SCREEN` are). Their numbers reach scripts and the API.
- **Names:** user-visible text says TitanVSP. Don't rename what code, files or settings depend
  on: the `openvsp` module, API names, exe and library names, `.vsp3` markers, `FC_OPENVSP`,
  and the `openvsp.org`/`VSP` preferences store (renaming it would reset users' settings).
- **Nacelles aren't special.** A nacelle is just a Fuselage, Pod or Stack. Don't add
  nacelle-specific code or wording.
- **Commits:** a short summary line, then a body explaining what changed and why, wrapped at
  about 78 columns. No AI co-author or "generated with" trailers.
- **Docs:** when a user-visible feature changes, update [CHANGES.md](CHANGES.md), and
  [README.md](README.md) if it covers that feature.

## Fillet component

`src/geom_core/FilletGeom.cpp` (about 2,400 lines) is the most involved part of the fork. Its
header comment gives the outline; this section covers what you need to change it safely.

### How it plugs into OpenVSP

- **Type:** `FILLET_GEOM_TYPE` in `APIDefines.h`. Registered in `Vehicle.cpp` (`GeomType(...,
  "FILLET", true)` and the `CreateGeom` branch). `Vehicle::AddGeom` refuses it unless the parent
  is a Wing or a body (`FilletGeom::IsBodyType`: Fuselage, Pod, Stack).
- **Screen:** `FilletScreen` (`FILLET_GEOM_SCREEN`), registered with the other geom screens in
  `ManageGeomScreen.cpp`. It shows the Design parms and `GetStatus()`.
- **Dependencies:** the fillet is a child of the part it hangs off, and a *step child* of the part
  it blends into (`SetFuse` adds and removes the step-child link; `m_FuseID` is saved in the
  `.vsp3` under `<Fillet><FuseID>`). In `Geom.cpp`, a parent or step parent whose surface or
  transform updates marks Fillet children `m_SurfDirty`. That's how it rebuilds.
- **Placement:** surfaces are built straight in world coordinates, so `UpdateCopyXFormParms`
  pins the fillet's own transform to identity and greys out transform and symmetry parms.
  Every surface of the parent, mirrored copies included, gets its own fillet surface.
- **Caps:** never. The surface closes itself (to a point, or round on itself as a ring), so
  `UpdateCopySurfParms` forces the cap options off.
- **Partner:** `FindFuse` keeps the current partner while it's still a valid candidate.
  Otherwise it picks the body holding most of a wing's root section, or, for a body, the body or
  wing it overlaps most (grid samples counted inside each other, both ways).

### Building blocks (anonymous namespace)

| Piece | Role |
| --- | --- |
| `SurfDist` | Signed distance to a set of surfaces (negative inside), with the nearest point and normal. Each surface is sampled once on a grid, and a global query polishes the few nearest samples with a local solve. **Never use `VspSurf`'s own global nearest-point search here**: at ~0.66 s a call it froze the app. Consecutive queries warm-start from the last answer; call `Reset()` before a query far from the previous one. |
| `SafeNormal` | A surface normal that survives degenerate spots (airfoil LE/TE, fuselage noses, zero-length LE patches) by averaging neighbours over a widening stencil. |
| `OffsetSide` / `OffsetPair` | A surface pushed out along its normal by R, and the pair of them. A ball of radius R touching both has its centre where the two offset surfaces meet, so Newton on the surfaces' **own (u, w) parameters** follows that "spine". This is what keeps contacts from jumping between a thin wing's upper and lower surfaces at the LE, which nearest-point queries do. |
| `Station` | One cross-section: junction point, ball centre, radius, both contacts and their parameters. |
| `BallInPlane` | A ball solved from *distances* in a plane across the junction, not from surface parameters. Used where parameters fail: a degenerate nose patch, creases. |
| `SkinStations` | Turns stations into the closed `VspSurf` (details below). Shared by wing and ring fillets. |
| `BuildFillet` | One wing surface's fillet: seed, march, refine, clean up, fill gaps, skin. |
| `JunctionLoops` | Between bodies: samples body b's signed distance on a grid over body a's surface, then traces the sign changes as closed loops (marching squares with saddle handling, periodic in w, Illinois regula falsi on edges). Loops that run off a's parameter edges are dropped. |
| `BuildRing` | One ring fillet round one junction loop. |

### Wing root fillet (`BuildFillet`)

1. **Junction:** along each chordwise line w of the wing, walk out from the root until it leaves
   the body (`FindJunction`). Stations use `LoopParams`: lower TE → LE → upper TE, cosine
   spaced.
2. **Seed and march:** one ball is found at the junction, then pinned to the surfaces and
   marched both ways with `OffsetPair` Newton, falling back to smaller steps when a solve fails.
   Each accepted ball must clear both bodies everywhere, not just touch them.
3. **TE taper:** the radius tapers to nothing over `TETaper` of each side. The two end stations
   collapse to single points a hair *short* of the TE, one on the upper surface and one on the
   lower, so they never coincide (two coinciding points make a non-manifold vertex that mesh
   booleans reject). Interior stations whose radius has shrunk to nothing are skipped, never
   collapsed: a second point inside an end pinches the fillet.
4. **Refine:** stations are added where the section turns quickly (`TurnAngle`), e.g. round the LE.
5. **Clean up for big balls on thin noses:** round a sharp LE, a ball much larger than the nose
   barely moves while the chordwise lines sweep round it. Stations that step backwards (in centre,
   contacts or junction point) or nearly repeat their neighbour are dropped. Remaining gaps the
   ball swings across are filled with `BallInPlane` balls in planes across the centres' path.
   Where the nearest point on the thin wing jumps, the in-between ball is built from its
   neighbours' contacts instead.
6. **Skin:** `SkinStations`, open (not closed).

### Ring fillet between bodies (`BuildRing`)

1. **Junction:** the loop from `JunctionLoops` (traced on the *smaller* body, whose grid
   resolves it better), smoothed into a closed curve. Points that crowd a neighbour are dropped
   first.
2. **Balls:** solved from the distances to both bodies, so the ball rolls smoothly over creases
   such as a fuselage's seam. When the other body is a **wing**, it's always body B, and its
   contact is held to the wing's own parameters, for the LE reason above.
3. **Corner check:** a ball is accepted only if it touches both bodies near the junction (about
   `r·tan(g/2)` from it for walls an angle g apart) and the way from the junction to its centre
   runs through open air. Otherwise Newton finds a ball in some other corner.
4. **March:** follows the spine of ball centres, not the junction: a predictor step along its
   tangent, then Newton back across it. Steps shrink where the spine or contacts turn quickly and
   grow back where they don't. The spine stays smooth where the junction turns sharply (a fuselage
   tail sinking into a wing). It runs all the way round back to the seed, and the last section
   mustn't crowd the first.
5. **Skin:** `SkinStations`, closed.

### Sections and skinning (`SkinStations`)

- **Section:** the ball's arc between its contacts is the visible face. `Sink` carries the face on
  past each contact along that surface, easing under it quadratically, so it fades in. The run
  shrinks with the ball toward a wing's TE and is capped relative to R. The section then closes
  through a buried point past the junction. The face is one smooth spline; the buried legs are
  straight.
- **Buried point:** it starts 0.3·R (plus sink dip) past the junction. Next to a thin body that
  would poke out the far side, so its depth is halved until `buried(j, p)` holds: no farther
  outside either body than the junction itself, within 0.01·R. Its depth is then eased along the
  junction (the least of its neighbours over ±2 stations, averaged). A sudden change in one
  section, even out of sight, makes the C2 skin overshoot and fold the visible face.
- **Skin:** C2, parameterised by the stations' spacing along the junction so refined stretches
  don't overshoot. Skinning can't close a surface, so a ring is skinned with a few sections
  repeated past each end and only the middle kept.
- **Relabel u:** OpenVSP's tessellation expects each surface section to span exactly one unit of
  u. Without the relabel, a short section gets no rows and leaves a pinhole in the mesh.
- **Orientation:** every station votes, from the middle of its arc, on which way is out. The face
  must look into the space the ball rolled through.

### Invariants: check these after any fillet change

- **Stay in range:** evaluate surfaces only inside their parameter ranges. Clamp, or wrap w on a
  closed body, and reject non-finite values. Evaluating outside a range corrupted memory before
  this was enforced.
- **Watertight STL:** the exported fillet must be closed, outward-facing and 2-manifold, with no
  open, non-manifold or degenerate edges.
- **Hidden closing edges:** a section's closing edges must stay buried in both bodies.
- **No regressions:** fillets that worked before should come out unchanged. Compare STL exports
  before and after.
- **Fails cleanly:** failure means an empty surface plus a helpful `m_Status`, never a crash or a
  hang.

## Dark theme

- **Palette (`ApplyDarkTheme` in `ScreenMgr.cpp`):** runs once before any screen is built. It sets
  FLTK's background, background2 and foreground, and remaps the fixed palette entries screens use
  for text (`FL_BLACK`, `FL_DARK_BLUE`, `FL_BLUE`, index 4, `FL_DARK_MAGENTA`) to light colours.
  It also remaps index 23 (read-only fields), index 12 (divider bars), the selection colour and
  tooltips. A widget that sets its own RGB colour isn't affected. That's why a few screens
  (`GroupLayout` dividers, `GuiDevice` tints, the attribute tree, the CompGeom/DegenGeom text) got
  explicit fixes. A new screen that hard-codes an RGB colour has to pick one that reads on dark.
- **Title bars (`DarkTitleBars`):** called from the GUI timer. It sets DWM attribute 20
  (immersive dark mode) once per new top-level window.
- **3D view:** the default background (38, 41, 46) is in `vsp_graphic/src/Background.cpp`.
  Axis, origin and colour-scale label colours are in `Viewport.cpp`.
- **Logo:** `Display::showLogo( true )` (set for the main window only, in `MainVSPScreen`) loads
  the logo once per window into a mipmapped texture, and `LayoutMgr` has each viewport draw it
  (`Viewport::drawLogo`) right after the background. The PNG's bytes are embedded in
  `vsp_graphic/include/TitanLogo.h`; to change the logo, regenerate that header from the new PNG
  (a `static const unsigned char TITAN_LOGO_PNG[]` of the file's bytes and its size).
- **Line colours:** the default component colour is `Color::Color()` in
  `geom_core/Color.cpp` (170, 170, 170). Selection highlights are in `Geom.cpp` (bounding box,
  active XSec, active sub-surface), `PickableGeom.cpp` and `Viewport::drawBorder`. The XSec View's
  curve colour is `Vehicle::m_XSecLineColor`, set in the constructor *and* `Wype()`; the
  constructor matters because opening a file at launch never calls `Wype()`.

## Perspective view

- **Camera (`vsp_graphic` `Camera.h/.cpp`):** keeps an orthographic projection
  (`getOrthoProjectionMatrix`) and an orbit matrix (`getOrbitMatrix`). In perspective mode
  `getProjection` uses a 30° perspective and moves the eye back by `_eyeDistance`, chosen so the
  plane through the centre of rotation fills the view as the orthographic view does. Zoom,
  rotate and pan therefore stay shared, and switching keeps the framing. `setZNearFar`
  recomputes this.
- **Ortho for screen work:** the screen grid and `screenToWorld` (rulers, probes, picking a
  location) in `Viewport.cpp` deliberately use the orthographic matrices in both modes.
- **Menu:** `MainVSPScreen` has the View → Perspective check item (key `5`). `SetPerspective()`
  applies it to every viewport, and it's reapplied in `Update()`.

## Plots (Results Manager)

- **Graph widget:** OpenVSP's graphs are `Vsp_Canvas` (`GuiDevice.h/.cpp`), built on the bundled
  Cartesian library (`src/external/cartesian`). `GroupLayout::AddCanvas` makes the canvas and its
  axes and sets the grid colours.
- **Pan/zoom/hover:** opt-in through `Vsp_Canvas::EnablePanZoom( view, reset )`. Without it the
  canvas forwards clicks and drags to its callback, which the curve editor relies on. While it's
  on, the canvas pans on drag, zooms on the wheel about the cursor, resets on a double click, and
  draws a crosshair and coordinate readout in `draw()`.
- **Keeping the view:** `VSPAEROPlotScreen::EnablePanZoom` writes each new view into that plot's
  `VSPAEROMgr` X/Y Min/Max parms and sets their `IsManual` flags. The screen's normal refresh
  (`Update*AutoManualAxisLimits`) then keeps the view. Reset clears the flags.
- **Zero axes:** `Ca_Canvas` has a TitanVSP hook, `virtual draw_underlay()`, called after the grid
  and before the plotted objects. `Vsp_Canvas` uses it to draw x = 0 and y = 0 light. Keep this
  hook when updating the Cartesian library.

## File chooser

`ScreenMgr::FileChooser` is the single entry point for every open/save dialog. It reads the
`File_Chooser` preference (`openvsp.org`/`VSP` store, `Application` group), which defaults to
`FC_NATIVE` (`Fl_Native_File_Chooser`). `FC_OPENVSP` gives the built-in `SelectFileScreen`, which
has favourite folders. The default appears in two places, `ScreenMgr.cpp` and
`PreferencesScreen.cpp`, and they must match.

## Upstream updates

History starts from a squashed import, so upstream releases go in as a diff between release tags,
not a merge:

```sh
git remote add upstream https://github.com/OpenVSP/OpenVSP.git   # once
git fetch upstream --tags
git diff --binary OpenVSP_3.55.0 OpenVSP_3.56.0 | git apply -3
```

Resolve conflicts in the files TitanVSP touches, then rebuild and re-check fillets, the theme and
the view. Never push upstream's tags to this repository. When the version changes, update the
version numbers in [README.md](README.md), [CHANGES.md](CHANGES.md) and the release zip name.

## Releases

Releases are on GitHub, tagged `v<openvsp version>-titan.<n>` (e.g. `v3.55.0-titan.2`), with the
`PACKAGE` zip attached and its SHA-256 in the notes. Make a new tag for each release rather than
moving an old one.
