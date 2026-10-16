TitanVSP is Titan Dynamics' build of [OpenVSP](https://github.com/OpenVSP/OpenVSP) 3.55.0. It adds a **Fillet** component that blends wings and fuselages into each other with smooth rolling-ball fillets, and reworks the day-to-day experience: a dark interface, a perspective 3D view, the system file dialogs by default, VSPAERO results you can pan, zoom and read values from, a VSPAERO Viewer that handles like the main 3D view and saves high-resolution images, Blender-style selecting, moving and rotating in the 3D view, a searchable Geom Browser, a plain Cylinder component, and more control over exports.

Each new release replaces the previous one, so this is always the latest TitanVSP.

## What's new

### Recently added

- **Outlines switch:** **View → Outlines** shows or hides the black outlines of every component at once.
- **Auto perspective, as in Blender:** Top, Front, Left, Bottom, Rear and Right (**F5**–**F7**, **F9**–**F11**) show orthographic; orbit the view and it goes back to perspective, if it was. Blender's view keys work too: **1** Front, **3** Left (nose to the left), **7** Top, **Ctrl** for the opposite side, keypad **5** for perspective.
- **No viewport border by default:** the white border round the active viewport is off; **Window → Border Toggle** turns it on.
- **Cylinder component:** pick **CYLINDER** in the Geom Browser. A closed, flat-ended cylinder set only by **Length** and **Diameter**: no sections, no blending. Fillets treat it as a body.
- **Geom Browser search:** a *Search...* field over the tree narrows it to the components whose names contain what you type, keeping their parents for context. **Enter** selects the matches. Fast with thousands of components.
- **Keyboard move and rotate:** with components selected and the mouse over the 3D view, **G** or **R**, then **X**/**Y**/**Z**, a number and **Enter** (`G X 12`, `R Z 90`). The axis is drawn as you type and the change previews live; **Esc** cancels. A hint in the bottom-left corner spells it out. Clicking a component keeps the keyboard on the 3D view.
- **Ground grid:** **View → Grid** shows a grid on the XY plane under the model, fading out around it, with the X and Y axes in red and green.
- **Anti-aliasing:** **View → Anti-aliasing** smooths lines in the 3D view; the black edge outline is thicker.
- **View settings per model:** Perspective, Grid, Anti-aliasing and Outlines are on for new models, and each model saves its own.

### Also in TitanVSP

- **Perspective view:** **View → Perspective** (key **5**), keeping the same framing.
- **CHANGES.md in the zip:** the full list of TitanVSP's changes ships alongside README.md.
- **Click to select:** click a component in the 3D view to select it; **Shift**+click adds or removes it; click empty space to clear the selection. Dragging still turns the view.
- **Move arrows:** a selected component shows Blender-style red, green and blue arrows at its origin. Drag one to move the selection along X, Y or Z, in whole units. Undo puts it back.
- **Selection look:** the selected component is drawn a little brighter, inside a thinner (3 px), half-transparent box.
- **Blanks as balls:** Blank components show as small half-transparent balls, the same size on screen at any zoom, ringed when selected and hidden behind parts in front of them. Click one to select it.
- **VSPAERO Viewer:** dark theme, the main 3D view's mouse controls (left-drag rotates, right-drag or Alt + left-drag pans, middle-drag or Ctrl + left-drag zooms, the wheel zooms about the cursor), and **File → SaveAs → PNG File** at any width through the Save dialog.
- **STL export resolution:** a **Modify Resolution** checkbox in the STL Options window to export at your own **Tess U**, **Section Tess U**, **Tess W** and **Cap Tess** without changing the model.
- **STEP and IGES** export in millimetres by default.
- **Titan Dynamics logo** in the top-right corner of the 3D view.
- **Fillet component** for wing roots and body-to-body joins: a separate, closed, watertight body that rebuilds whenever either part changes.
- **Dark theme** throughout, including title bars and the 3D view, with light component lines and white selection highlights.
- **System file dialogs by default** for every open, save, import and export: OpenVSP's existing **Native** file chooser preference is now the default.
- **VSPAERO results plots:** drag to pan, wheel to zoom about the cursor (Ctrl for x only, Shift for y only), double-click to reset, hover to read x and y; light x = 0 and y = 0 axes.

The full list is in [CHANGES.md](https://github.com/Titan-Dynamics/TitanVSP/blob/main/CHANGES.md).

## Install

1. Download **TitanVSP-3.55.0-win64.zip** below (64-bit Windows).
2. Unzip it somewhere you can write to, such as Documents, not Program Files. TitanVSP saves files next to itself by default.
3. Run `vsp.exe` in the `TitanVSP-3.55.0-win64` folder. The program isn't signed, so Windows SmartScreen may say "Windows protected your PC". Click **More info → Run anyway**.

Nothing else needs installing; the Visual C++ runtime is included.

## Using fillets

1. In the Geom Browser, select the wing or fuselage.
2. Pick **FILLET** in the type list next to **Add**, then click **Add**.
3. On the Fillet's **Design** tab, a status line says what it's blending into. The parameters:
   - **Radius**: the size of the ball rolled along the junction. 0 picks one automatically: 8% of the wing's root chord, or of the slimmer body's thickness.
   - **Stations**: the number of cross-sections round the junction.
   - **TE Taper**: wing roots only. How far from the trailing edge the radius tapers to nothing.
   - **Sink**: how far the fillet's face carries on past each contact, easing under the surface so it fades in. It's in model units, so turn it down on small models.

From Python or AngelScript: `AddGeom("FILLET", parent_id)`. The parameters are `Radius`, `NumStations`, `TETaper` and `Sink` in the `Design` group.

## Python API

The bundled Python API (in the `python` folder) only loads in **64-bit Python 3.11**. The GUI and `vspscript.exe` don't need Python.

## Known limits

- Wing fillets only blend the wing's root (its first section) into a body.
- A body join can't run across a flat end cap or a sharp edge, such as a boom exiting through a fuselage's flat tail cap.
- If the ball can't fit all the way round a junction, the status line says so; try a smaller Radius.
- As in OpenVSP, an STL export turns the exported components into a Mesh and hides them; exporting again writes that Mesh. To export at another resolution, delete the Mesh and show the components again first.

## Notes

- The app names itself TitanVSP 3.55.0. Scripts' `GetVSPVersion()` returns `TitanVSP 3.55.0`, and exported CAD and mesh files say they came from TitanVSP. Models, scripts and the `openvsp` Python API work as in OpenVSP 3.55.0; fillets are TitanVSP's own.
- TitanVSP is a modified version of OpenVSP, distributed under the same license, the NASA Open Source Agreement 1.3 (included as `LICENSE`).
- Source, build instructions and the full change list are in the [README](https://github.com/Titan-Dynamics/TitanVSP#readme).
