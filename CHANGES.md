# Changes in TitanVSP

TitanVSP is [OpenVSP](https://github.com/OpenVSP/OpenVSP) 3.55.0 with the changes below. Its
history starts with one commit importing OpenVSP 3.55.0 (upstream tag `OpenVSP_3.55.0`); every
commit after that is TitanVSP's own.

**Compatibility.** File formats, the `openvsp` Python module and the API work as in OpenVSP 3.55.0.
TitanVSP adds to them: the `FILLET` component, and STL export settings on the Vehicle. A few
defaults differ for new models: component colour, the Fillet's TE Taper, and the STEP and IGES
length unit. Models keep whatever they were saved with.

- [Releases](#releases)
- [Fillet component](#fillet-component)
- [Dark theme](#dark-theme)
- [3D view](#3d-view)
- [Windows file dialogs](#windows-file-dialogs)
- [VSPAERO results plots](#vspaero-results-plots)
- [VSPAERO Viewer](#vspaero-viewer)
- [Export options](#export-options)
- [TitanVSP name and logo](#titanvsp-name-and-logo)
- [Windows build scripts and Python API](#windows-build-scripts-and-python-api)
- [Fixes to upstream behaviour](#fixes-to-upstream-behaviour)
- [Removed](#removed)

## Releases

| Release | What's in it |
| --- | --- |
| Not yet released (`main`) | Keyboard move and rotate (G / R), the ground grid, the anti-aliasing switch, the thicker outline, and the 3D view settings (perspective, grid, anti-aliasing) on by default and saved with each model. |
| [v3.55.0-titan.7](https://github.com/Titan-Dynamics/TitanVSP/releases/tag/v3.55.0-titan.7) | Everything else on this page. The zip includes this file, `CHANGES.md`. |
| v3.55.0-titan.6 (withdrawn) | titan.7 without `CHANGES.md` in the zip. Replaced by titan.7. |
| v3.55.0-titan.5 (withdrawn) | titan.6 without perspective by default, saved with each model. Replaced by titan.6. |
| v3.55.0-titan.4 (withdrawn) | titan.5 without the move arrows, click-to-select, Blanks drawn as balls, and the brighter selection with thinner selection boxes. Replaced by titan.5. |
| v3.55.0-titan.3 (withdrawn) | titan.4 without the VSPAERO Viewer changes, the logo, STL export resolution, STEP and IGES in millimetres, and the Fillet's TE Taper default of 0.75. Replaced by titan.4. |
| v3.55.0-titan.2 (withdrawn) | titan.3 without the VSPAERO results plot changes. Replaced by titan.3. |
| v3.55.0-titan.1 (withdrawn) | The Fillet component, the TitanVSP name, and the Windows build scripts. Replaced by titan.2. |

## Fillet component

A new component type, **FILLET**, blends one part into another with a smooth, concave
rolling-ball fillet: the shape a ball of fixed radius sweeps as it rolls along the junction
touching both parts.

| Add the Fillet to | It blends into | Shape |
| --- | --- | --- |
| a **Wing** | the Fuselage, Pod or Stack its root is buried in | Runs round the wing root from the lower trailing edge to the upper one, and tapers to a point at each end of the trailing edge |
| a **Fuselage, Pod or Stack** | the Fuselage, Pod, Stack or Wing it overlaps most | A closed ring round each loop where they meet; a body passing right through another gets two rings |

- **A separate body.** The fillet is its own closed, watertight body overlapping both parts, and
  it leaves them untouched. Exported to STL, it's a closed, outward-facing 2-manifold, so it
  unions cleanly with the parts in Blender and other mesh tools.
- **Finds its partner.** The other part is found on its own and saved with the model. The fillet
  rebuilds whenever either part moves or changes.
- **Built in place.** Its position, rotation and symmetry are fixed and greyed out, because it's
  built in world coordinates. Each mirrored copy of a symmetric part gets its own fillet.
- **Parameters** (all in the `Design` group):

  | Parameter | Default | What it does |
  | --- | --- | --- |
  | `Radius` | 0 (auto) | Ball radius. At 0 the first build picks 8% of the wing's root chord, or 8% of the slimmer body's thickness, and fills it in. |
  | `NumStations` | 41 (9–401) | Cross-sections round the junction. More are added wherever the fillet turns quickly. |
  | `TETaper` | 0.75 (0–1) | Wing roots only: the fraction of each side over which the radius tapers to nothing at the trailing edge. 0 and greyed out on body fillets. (0.15 in titan.3 and earlier.) |
  | `Sink` | 1.0 | How far the fillet's face carries on past each contact, easing under the surface, so it fades in instead of ending on a line. In model units, capped at 4 × Radius. |

- **Status line.** The Fillet's **Design** tab says what it's blending into, or why it couldn't
  build: the root isn't in a body, or the ball doesn't fit all the way round a junction.
- **API.** `AddGeom("FILLET", parent_id)` from Python, AngelScript or C++. It fails if the parent
  isn't a Wing, Fuselage, Pod or Stack. Scaling the model scales `Radius` and `Sink`.
- **Speed.** A rebuild takes about 0.1–1.5 s, depending on the junction.

How it works is described in [AGENTS.md](AGENTS.md#fillet-component) and at the top of
`src/geom_core/FilletGeom.cpp`.

**Limits:** a wing fillet only blends the wing's root (its first section) into a body. A body
join must be a closed loop that doesn't cross a flat end cap or a sharp edge. Upstream OpenVSP
doesn't know the Fillet component.

## Dark theme

The whole interface is dark.

- **Windows and controls:** dark backgrounds, darker inputs and lists, light text and blue
  selection. Section divider bars, read-only fields, tooltips, the console, the attribute tree
  and the CompGeom and DegenGeom result panels all follow the theme.
- **Title bars:** every window asks Windows for a dark title bar (Windows 10 and later).
- **3D view:** the default background is dark slate. Axis labels and colour-scale labels are
  light. **Reset** in Background settings goes back to the dark default.
- **Line colours:** new components draw in light grey (#aaa) instead of blue. Cross-section and
  sub-surface lines that were black are #aaa too. The selected component's outline, its active
  cross-section, the active sub-surface, the picking hover highlight and the selected viewport's
  border are white instead of red. A wing's active-section box stays green. The selection boxes
  are thinner (3 px, was 4) and half transparent, and a selected component is drawn a little
  brighter: its lines a third of the way to white, its shading lifted slightly. Models keep the
  colours saved in them, so only new components pick up #aaa.
- **XSec View:** the airfoil window opened by **Show** draws its curve in white, and its
  **Reset Defaults** button (and the Curve Editor's) goes back to white.
- **Graphs:** the grid of OpenVSP's graphs (the Results Manager, Wave Drag and others) is shaded
  for the dark background.

The VSPAERO Viewer has its own dark theme; see [VSPAERO Viewer](#vspaero-viewer).

## 3D view

**Ground grid.** **View → Grid** shows a grid on the XY plane (z = 0), centred under every
component in the model, shown or hidden. It looks endless: solid near the model, fading out slowly
to nothing at 110% of the model's largest dimension from its middle. Lines are on whole multiples of
a round spacing (1, 2 or 5 times a power of ten) that gives about twenty cells across the model,
with every fifth line brighter and the X and Y axes in red and green. Parts in front of the grid
hide it.

**Anti-aliasing.** **View → Anti-aliasing** smooths the lines in the 3D view, for setups where
multisampling isn't available and wireframes come out jagged. The black outline drawn along a
component's edges is 2 px instead of 1.

**Saved per model.** Perspective, Grid and Anti-aliasing are all on for new models (and for files
saved before they existed), and each model saves its own settings, so a file reopens the way it
was left. They're the Vehicle's `Perspective`, `Grid` and `Antialias` settings, in the
`ViewSettings` group.

**Click to select.** Clicking a component in the 3D view selects it, as if picked in the Geom
Browser, and opens its window. **Shift**+click adds it to the selection, or takes it out; clicking
empty space clears the selection. A click is a press and release that hardly moves, so dragging
to turn the view works as before, anywhere. It uses the same picking as the Geom Browser's
**Pick** button, but only for the click, so nothing highlights as the mouse passes over.

**Blanks.** A Blank component has no surface; it used to show as a small axis triad. It's now a
small shaded ball at its origin (and at each symmetric copy), half transparent and the same size on
screen whatever the zoom or window, with a white ring round it when selected. Clicking the ball
selects the Blank. Parts in front of a Blank hide its ball, as they hide each other. The Geom
Browser's **Pick** mode still picks Blanks by their (now invisible) triad.

**Keyboard move and rotate.** As in Blender, with components selected and the mouse over the 3D
view: **G** (move) or **R** (rotate), then **X**, **Y** or **Z**, then a number, then **Enter**.
`G X 12` moves the selection 12 units along X; `R X 45` rotates it 45° about X. Choosing an axis
draws it as a line through the selection in its colour, a prompt in the top-left corner shows
what's being typed, and the change previews as you type (`-` flips the sign, Backspace deletes).
**Enter** or a left click applies it; **Esc** or a right click cancels. Without an axis nothing
changes. Pressing **G** or **R** again switches between move and rotate. Selecting a component
by clicking it keeps the keyboard on the 3D view, so G and R work straight away even though its
window opens. A move is along the
model's axis, as with the move arrows; a rotation adds to that axis's rotation (X/Y/Z Rotation,
or Rel Rotation for a component placed relative to its parent). One undo puts it back. With nothing
selected, **R** still sets the rotation center (View → Set Rotation Center). **View → View Front Of** no longer has
the **X** shortcut, which belongs to the move and rotate; it's still in the View menu. Two dim lines in the bottom-left corner of the 3D view spell out the
G and R shortcuts.

**Move arrows.** Selecting components shows Blender-style move arrows at their origin: red X, green
Y and blue Z, at a fixed size on screen, drawn over the model. Drag an arrow with the left mouse
button to move the selection along that axis, in whole units from where it started. The arrow under
the cursor brightens, an axis pointing straight at the viewer is hidden, and components whose
location can't be set, such as Fillets, aren't moved. A component placed relative to its parent
still moves along the model's axis; its Rel Location takes the move through the parent's
attachment rotation. **Undo** puts a dragged component back where the drag began (one undo for
each location value the drag changed), and the arrows show only in the main 3D view.

**View → Perspective** (key **5**) switches every viewport between orthographic and perspective
(30° field of view). The perspective eye stands back far enough that the view keeps the same
framing, so rotate, pan and zoom carry over and switching back and forth doesn't jump. The screen
grid, rulers, probes and picking work the same in both modes.

The view starts in perspective; each model saves the mode it was left in (see *Saved per model*
above).

## Windows file dialogs

Open, save, import and export use the operating system's own file dialog by default, instead of
OpenVSP's built-in chooser. The built-in one, with its favourite folders, is still available under
**File → Preferences… → File Chooser**. A choice already saved there is kept.

## VSPAERO results plots

The plots in **Results Manager - VSPAERO** (Convergence, Load Dist., Sweep, Cp Slice and
Unsteady) can be explored with the mouse:

- **Pan:** drag the plot.
- **Zoom:** roll the mouse wheel to zoom in or out about the point under the cursor. Hold
  **Ctrl** to zoom only along x, or **Shift** to zoom only along y.
- **Reset:** double-click to go back to the automatic fit.
- **Read values:** hovering shows a crosshair and the x and y under the cursor in the plot's
  top-right corner.

Panning or zooming sets the plot's Xmin, Xmax, Ymin and Ymax under **Actions** to manual at the
new limits, so the view stays put when the plot refreshes. Unticking them, or double-clicking,
hands them back to the automatic fit.

The lines x = 0 and y = 0 draw light over the grid. This applies to every graph that uses the
same widget, such as Wave Drag.

## VSPAERO Viewer

The VSPAERO Viewer (`vspviewer.exe`, opened from the VSPAERO screen) is a separate program; it
gets the same treatment.

- **Dark theme:** its panels, menus, buttons and title bar use TitanVSP's palette, and its 3D view
  opens on the same dark slate, with light text and legend. Trailing wakes draw light blue.
  **View → Use White Background** (formerly *Use Blue Background*) switches the view to white,
  with the original colours, for printing or screenshots.
- **Mouse:** the 3D view handles like TitanVSP's main view. Left-drag rotates; right-drag or
  Alt + left-drag pans, with the model following the cursor (it used to move a fixed step per
  pixel); middle-drag, Ctrl + left-drag or left + right drag zooms; and the mouse wheel, which did
  nothing, zooms 15% a notch about the point under the cursor.
- **Saving images:** **File → SaveAs → PNG File** asks for the image width, showing the height
  that follows from the view's aspect ratio greyed out, then opens the Windows Save dialog in the
  results' folder. The image is rendered off screen at that size, so it can be larger than the
  window; text and the legend scale with it. It used to ask for a bare name in a text box and
  always save `<results>.<name>.png` at the window's size. If the graphics card can't render off
  screen, the window's size is saved and the user is told. Saved images are opaque, as on screen.

## Export options

- **STL resolution:** the STL Options window has a **Modify Resolution** checkbox, off by
  default. Ticked, it exports at the resolution entered below it instead of each component's
  own: **Tess U** (components without sections, such as pods and fillets), **Section Tess U**
  (every section of wings, fuselages and stacks), **Tess W** and **Cap Tess** (end caps). Each
  component in the export takes them for the export only, then gets its own settings back. The
  settings save with the model, and the API's STL export uses them too (`ResOverride`, `TessU`,
  `SectTessU`, `TessW` and `CapTess` in the Vehicle's `STLSettings` group).

  As in upstream OpenVSP, an STL export turns the exported components into a Mesh component,
  leaves it in the model and hides the originals; exporting again writes that mesh. To export
  at another resolution, delete the Mesh and show the components again first.
- **STEP and IGES units:** new models export STEP and IGES files in millimetres instead of feet.
  Models that were saved with another unit keep it.

## TitanVSP name and logo

The app calls itself TitanVSP 3.55.0 everywhere a user sees a name: window titles, the `-help`
banner, `GetVSPVersion()`, the headers of exported STEP, IGES, NASTRAN, Calculix and DXF files,
preferences and messages, and the release zip (`TitanVSP-3.55.0-win64.zip`).

Kept as they were, because code, files or settings depend on them: exe and library names, the
`openvsp` Python module and API names, file format markers (`.vsp3`, `OPENVSP_XSEC_FILE_V1`
and so on), the `FC_OPENVSP` enum, the preferences store, links to the OpenVSP wiki and help,
and the macOS and Linux package names.

The Titan Dynamics logo sits in the top-right corner of the main 3D view, behind the model, about
a fifth of the view's width. It's built into the program, so there's no file to keep with it, and
it scales smoothly at any window size. Other 3D windows, such as the XSec View, don't show it.

## Windows build scripts and Python API

- `dev\build.ps1` builds the third-party libraries (first time only) and TitanVSP, and installs
  it next to the repo. `-Clean` and `-CleanAll` rebuild from scratch.
- `dev\run.ps1` does an incremental build and launches `vsp.exe`. `-NoBuild` just launches it.
- `dev\install-shortcut.ps1` adds a one-click "TitanVSP (Dev)" shortcut that runs `run.ps1`.
- The install folder and release zip include `CHANGES.md` alongside `README.md`, so the list of
  TitanVSP's changes ships with the program.
- The Windows build enables the Python API and sets up a venv with the `openvsp` package installed
  in editable mode, so it picks up rebuilt bindings automatically.
- The packaged Python `environment.yml` pins the Python version the bindings were built for.
  Upstream left it blank, so conda could build an environment the bindings couldn't load.

See [README.md](README.md#build-and-run) and [dev/README.md](dev/README.md).

## Fixes to upstream behaviour

- **Perspective:** the perspective code already in OpenVSP passed degrees where radians were
  needed. It has been replaced by the switch described under [3D view](#3d-view).
- **Time Step box:** in the Results Manager, the greyed-out Time Step box could show
  -2147483648 instead of 0 when there were no unsteady results: a Parm limit of 1e12 overflowed
  when converted to an integer. Index selectors now clamp their limits.

## Removed

Nothing from upstream OpenVSP 3.55.0 is removed. An early TitanVSP command, **Model → Blend Wings to
Fuselages** (`BlendWingsAndFuselages` in the API), inserted thin airfoil sections near each wing
root. The Fillet component replaced it before the first release.
