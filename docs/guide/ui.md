# The UI toolkit

Status: draft (Phase 15, Days 184 and 189). The module and its canvas and draw list exist; widgets arrive from Day 192. Design and reasons: `docs/superpowers/specs/2026-11-10-ui-design.md`.

## The model

- The toolkit lives in `engine/ui` (namespace `engine_placeholder::ui`). It is **CPU only**: a retained tree of widgets, layout, themes, input routing and a 2D draw list. It names no
  RHI type; the renderer (`renderer::UiRenderer`) turns the draw list into one render-graph pass.
- A **surface** owns a widget tree, a size in **logical units**, a **scale** factor, a clock, a theme and the focus. A window is only one kind of thing that feeds a surface; an offscreen
  texture (an in-world screen) is another, and tests use one without any window.
- Everything is main-thread only.

## Drawing: the canvas and the draw list

Widgets draw on a **`ui::Canvas`**, an abstract class in logical units (`ui/canvas.h`): `DrawRect`, `DrawRoundedRect` (a radius per corner), `DrawBorder`, `DrawLine`, `DrawTriangle`, `DrawImage`, `DrawText`, `DrawShadow`, a clip stack
(`PushClip`/`PopClip`) and layers (`BeginLayer(order)`/`EndLayer`). No draw-list or GPU type appears in it, so a vector canvas, a software rasteriser or an exporter can replace the implementation without touching a widget.

- **`ui::DrawListCanvas`** is the implementation that ships: it converts to physical pixels and appends to a **`ui::DrawList`** (40-byte vertices, 32-bit indices, commands). **`ui::RecordingCanvas`** (`ui/testing/`) records the calls, so a
  widget test can assert "the button drew a rounded rect with the hover colour" without reading vertices.
- **Snapping (the contract of every canvas):** rectangles round their *edges* (`round(x * scale)`, `round((x + w) * scale)`), so neighbours share an edge; thicknesses (borders, strokes) are scaled and rounded, at least one pixel: a 1-unit
  border is 1 pixel at scale 1 and 1.25, 2 at scale 2. Radii and blur are scaled, not rounded. Text is laid out by the caller in physical pixels (logical size times `Scale()`).
- **One shader, three modes** (`Vertex::flags`): solid, rectangle (signed-distance rounded box with analytic anti-aliasing; a border is the same quad with `kFlagRing`, a shadow with `kFlagShadow`), glyph coverage from an atlas page and image
  colour. A rectangle and a glyph share one pipeline, so a command splits only on clip, layer, atlas page or image; a draw that needs no texture joins the previous command.
- **Clips and layers:** a clip intersects its parent and a draw outside it makes no vertex. A layer draws above every lower layer whatever the call order and starts unclipped, so a popup is not cut by the widget that opened it; call
  `DrawList::SortLayers()` once after drawing.
- **A frame allocates nothing** once the arrays have grown: `DrawList::Clear()` keeps the capacity. Past `max_vertices` a draw is dropped and counted in `stats.overflowed`; an index never wraps.

```cpp
ui::DrawList list;
ui::DrawListCanvas canvas(&list, &atlas);   // the glyph atlas for text; null makes text count as dropped
list.Clear();
canvas.Begin(2.0f, width, height);          // scale 2, surface size in physical pixels
canvas.DrawRoundedRect({10, 10, 120, 32}, ui::CornerRadii(6), 0xFF3355CCu);
canvas.DrawBorder({10, 10, 120, 32}, ui::CornerRadii(6), 1, 0xFFFFFFFFu);
list.SortLayers();                          // then the renderer draws list.commands in order
```

## Drawing on the GPU: the UI renderer

**`renderer::UiRenderer`** (`engine/renderer/ui_renderer.h`) turns a finished `ui::DrawList` into one render-graph pass over a colour target. It is a `Recreatable` at the materials tier, so a device reset rebuilds its buffers, pipeline and
the 1x1 white texture that textureless commands bind; it keeps nothing but CPU-side state.

- **One pass per frame:** `AddToGraph(graph, &list, target, format, width, height)` declares the pass (`ReadWrite` on the target, `LoadOp::Load`, so it draws over what is there) and declares nothing for an empty list. The pipeline follows
  the target's format: an sRGB target gets the colour decoded in the vertex shader, a UNORM one does not, and a format change rebuilds the pipeline.
- **Blending is premultiplied** (`One`, `OneMinusSrcAlpha`); the fragment shader outputs `rgb * a`.
- **Text** reads the glyph pages of a `GlyphAtlasTexture` (the same atlas that `ui::DrawListCanvas` filled), so the pixels match the text renderer's.
- **Images** are drawn through an `ImageSource`: register it with `SetImage(id, &source)`; the renderer asks for the texture handle every frame and never keeps it. A command whose image has no source or no texture is skipped and counted in
  `Stats().dropped_commands`.
- **Budget:** `UiRendererDesc::max_vertices` / `max_indices` size the per-frame-slot buffers; commands past them are dropped and counted, with a warning in the log.
- **Shaders** are `shaders/ui.vert.slang` and `ui.frag.slang`, minted to SPIR-V; the DXIL for Direct3D 12 arrives with Day 205 (`KNOWN-ISSUES.md`).

```cpp
renderer::UiRenderer ui_renderer;
ui_renderer.Create(device, &glyph_pages, renderer::UiRendererDesc{}, &device_resources);
// each frame, after the canvas drew and list.SortLayers() ran:
ui_renderer.AddToGraph(&graph, &list, backbuffer, format, width, height);
```

## Themes

Widgets do not hold colors or pixel sizes; they ask a **`ui::ThemeSet`** (`ui/theme.h`) for their look. A **`ui::Theme`** is plain data: named colors (`0xAABBGGRR`), metrics and fonts (logical units), and style entries per **widget kind** and **variant**
(`button`, `button/primary`) with a table over the states (normal, hover, pressed, focused, disabled, checked). Every value may be unset so that a theme can be a small override.

- **Resolution order**, least specific first: the built-in light theme, the surface's theme (`ThemeSet::SetTheme`), then the override themes of the widget's ancestors and of the widget. A theme's default style of the kind is laid over the result before
  its variant's; a state lays only what it sets over the normal style (`Disabled` strongest, then `Pressed`, `Hover`, `Focused`, `Checked`). An unknown variant resolves to the kind's default style, counts in `Stats().missing_styles` and logs once.
- **Scale:** `Resolve(kind, variant, state, scale)` multiplies sizes by the surface scale and rounds borders and paddings to whole pixels (a 1-unit border is 1 pixel at 1x and 1.25x, 2 at 2x; a border never vanishes); the radius is scaled, not rounded.
- **Swapping:** `SetTheme` bumps `Revision()`, drops the cache and returns whether any metric or font changed (then the caller marks layout dirty). A widget re-resolves when the revision it saw differs. A style resolved without overrides is
  cached by (kind, variant, state, scale); with overrides it is computed each time.
- **Kinds are an open registry:** `RegisterWidgetKind("gauge")` returns a stable id, and a game's widget reads the same theme data. `DefaultLightTheme()` and `DefaultDarkTheme()` are ordinary `Theme` values (the tests hold their text and button colors to
  a contrast ratio of 4.5:1); build your own with `ThemeBuilder`.

```cpp
ui::ThemeSet themes;
themes.SetTheme(&ui::DefaultDarkTheme());
ui::ResolvedStyle s = themes.Resolve(ui::widget_kind::kButton, ui::RegisterVariant("primary"), ui::kStateHover, surface_scale);
canvas.DrawRoundedRect(rect, ui::CornerRadii(s.radius), s.background);
canvas.DrawBorder(rect, ui::CornerRadii(s.radius), s.border_width, s.border_color);
```

## The widget tree, the surface and the test harness

A **`ui::Widget`** (`ui/widget.h`) owns its children (`UniquePtr`), draws on a `ui::Canvas`, and is addressed by a stable **id**: a hash of its parent's id and its name (its index among the siblings when the name is empty), so a tree built the
same way twice has the same ids. Bounds are in surface coordinates, in logical units. Children draw in order (the last is on top) and are hit-tested in reverse; a widget clips its children to its bounds by default, in drawing and in hit testing, and
`SetPassThrough(true)` lets hits go through the widget itself to what is under it. `MarkDirty(kLayoutDirty | kDrawDirty)` flags the widget and its ancestors and tells the surface; layout implies draw.

A **`ui::Surface`** (`ui/surface.h`) is the root of a tree plus what the widgets share: size, scale, clock, `ThemeSet`, `FontProvider`, the focus owner, the pointer-capture owner, the hover widget and a popup layer (hit first, drawn last). **It names no
window**: a window adapter, a test or an in-world screen feeds it `ui::Event`s with `Post` and runs `Frame(dt, &draw_list, &atlas)`: apply the queued events, call `Update` on the widgets whose `WantsUpdate()` is true, run layout if dirty, rebuild the
draw list if anything looks different or the list is empty (it returns whether it did; `NeedsRedraw()` is what an on-demand offscreen surface reads).

- **Routing:** pointer events go to the capture owner, else to the deepest widget under the pointer; a press captures the pointer to the widget it hit until the last button is up, and sets its pressed bit. Key and text events go to the focus owner
  (`SetFocus`, which sends `FocusLost`/`FocusGained`). An event a widget does not mark `handled` bubbles to its parents; it arrives with its position in the receiving widget's local space. Enter, leave and focus events go to one widget only. A
  disabled widget takes an event and does nothing with it.
- **Removal is safe in a handler:** while the surface dispatches, `RemoveChild`/`RemoveFromParent` queue the removal and the widget dies when the dispatch ends. Destroying a widget clears the focus, capture and hover references to it first.
- **Layout** is a hook only here: `Widget::Arrange()` runs parent before children when layout is dirty (Day 193 adds the containers).

**`ui::Harness`** (`ui/testing/harness.h`) is the offscreen surface every widget test uses: a font provider serving one TrueType file, a glyph atlas, the last draw list, input helpers (`PointerMove`, `Click`, `Key`, `TypeText`, `Wheel`; each runs a frame),
`Find`/`RectOf`, and `Render`, which draws the list with a small software rasteriser that follows the UI shader (rectangles, rounded boxes, rings, shadows, glyphs; images are skipped). A widget test compares rectangles or pixels without a GPU.

```cpp
ui::Harness h(200, 100, 2.0f, font_file);          // 200x100 logical units at scale 2
h.GetSurface().Root()->AddChild(MakeMyButton("ok", {10, 10, 80, 24}));
h.Click(20, 20);                                    // pointer move, press, release; frames run
ui::RasterImage image;
h.Render(&image);                                   // 400x200 pixels, compare what you expect
```

## Layout

Layout is two steps per widget, in logical units. **`Measure(constraints)`** says what size the widget wants from its content (a text block's text, a container's children); a container asks a child with **`MeasureTotal`**, which applies the child's
own `LayoutProps` (`width`/`height` preferences, `min`/`max`) and caches the answer for the layout pass. **`Arrange(rect)`** places the widget and its children. The surface runs it parent before children when layout is dirty, in at most **two passes**
per frame (the second is for a widget whose size depends on the width it was given and invalidates layout again); a widget that never settles is counted in `LayoutOverruns()` and the surface draws anyway. `Resize` to the size it already has does nothing,
and any number of resizes between two frames cost one layout. A plain `Widget` keeps its children where they are (or gives the whole content box to a child with `fill_parent`); the containers below place them.

- **`BoxLayout`** (horizontal or vertical): spacing and padding; each child gets its preferred size along the axis, then the free space goes to the children with a `weight` (a child that reaches its `max` is frozen and the rest share what is left);
  with too little room every child gives up a share of what it has above its `min`; across the axis a child follows its **alignment** (`Start`, `Center`, `End` use the preferred size, `Stretch`, the default, fills the cell within `min`/`max`).
  For a fixed size use `min == max` or a non-stretch alignment. **`Spacer`** is an empty widget with a weight.
- **`StackLayout`:** every child over the whole content box, aligned by its own properties. **`AnchorLayout`:** edges as fractions of the box plus offsets (`LayoutProps::anchor`; equal min and max on an axis means the preferred size).
- **`GridLayout`:** columns and rows of `TrackSize::Fixed(size)`, `Auto()` (the largest single-cell child) or `Weight(w)` (the preferred size plus a share of the free space); a child names `column`, `row` and spans, which do not size auto tracks.
- **Collapsed or hidden:** `SetCollapsed(true)` takes no space and is not drawn or hit; `SetVisible(false)` keeps the space.
- **`TextBlock`** reports the size of its text, laid out in physical pixels and converted back; with wrapping the width it is measured at is the wrap width, so a container measures it at the width it will give it. Its layout is cached, so an unchanged block does
  not lay out again.
- Rectangles stay fractional; the canvas snaps edges to whole pixels when drawing, so neighbours share an edge at 1.25x with no seam.

```cpp
auto* box = new_widget<ui::BoxLayout>(ui::Orientation::Horizontal);   // however your code makes widgets
box->SetSpacing(8);
ui::LayoutProps grow; grow.weight = 1;
box->AddChild(MakeSearchField())->SetProps(grow);                    // takes the free width
box->AddChild(MakeButton("Go"));                                     // its preferred width
```

## Input: focus, capture, Tab and the window

- **Pointer:** the hover is the topmost widget under the pointer, the popup layer first. Enter and leave follow it, also when the widget under a still pointer changes because of layout, visibility or a popup (the surface looks again after each frame that changed anything). A press captures the pointer to the widget it hit until every button is up, so a drag outside the surface keeps reporting to it and the release is delivered to it; `RequestCapture()` and `ReleaseCapture()` let a widget start or end a capture itself.
- **Wheel:** to the hovered widget (the capturer during a drag), bubbling to the first ancestor that sets `handled`; a widget behind a popup never sees it. Which axis a widget consumes is its own decision: it reads `wheel_x` or `wheel_y` and handles only the one it scrolls.
- **Keyboard focus:** one owner per surface. A click gives it to the nearest `Focusable()` enabled widget under the pointer (a click on nothing focusable clears it). Keys go to the owner and bubble to its ancestors. `Tab` and `Shift+Tab`, if nobody handled them, call `FocusNext`: positive `TabIndex` ascending, then `0` in tree order, negative skipped, disabled and hidden skipped, wrapping at both ends; with a popup open only the popup layer is walked. When the last popup closes the focus returns to the widget that had it when the first one opened.
- **Text and modifiers:** `Text` events go only to a focus owner with `AcceptsText()`. `Surface::Modifiers()` mirrors the last event's modifiers and becomes 0 when the window loses the focus.
- **Cursor:** a widget names a shape with `SetCursor`; `Surface::RequestedCursor()` is the capturer's, else the hovered widget's, else the nearest ancestor's, else Arrow.
- **Window focus:** posting `WindowFocusLost` releases the capture, closes the popups, clears the hover and the modifiers and sends `FocusLost` to the focus owner (which keeps the focus for when the window returns, `WindowFocusGained`). Keys do not reach a surface whose window has no focus; an offscreen surface is focused by default.
- **The window adapter** (`ui_window/window_adapter.h`, library `ui_window`, the only code that names both `ui` and `os::Window`) drains `Window::PollEvent` into the surface, dividing pointer positions by the surface scale, forwards resizes (several in one pump cost one layout) and, after the frame, sets the window's cursor only when the shape changed:

```cpp
ui::WindowAdapter adapter;
while (!window.ShouldClose()) {
    window.PumpEvents();
    adapter.PumpInto(window, surface);
    surface.Frame(dt, &list, &atlas);
    adapter.UpdateCursor(window, surface);
}
```

## Widgets: labels, images, bars, panels and buttons

All of these draw from the theme (`ThemeSet::Resolve` for their kind, variant and state; named colors and metrics for the rest). A gate (`ui_widget_style_check`) fails on a hex color or a number of 3 or more in `engine/ui/widgets/*.cpp`, so a widget cannot carry its own look; a constant that is a multiple of a theme metric (the progress bar's length in icons) is named in its header. A state change (hover, pressed, focus, checked, disabled) repaints the widget and never lays out.

- **`Label`** (`ui/widgets/label.h`): text from the `label` style; `SetVariant("heading")`, `SetWrap`, `SetAlign`, `SetEllipsis` (a single line wider than the bounds is cut at a cluster boundary and ends with "…"). A disabled label uses the disabled color.
- **`Image`**: an `ImageId` with its natural size, `ImageFit` `Stretch`, `Contain`, `Cover` or `None`, and a tint; anything outside the bounds is cut by narrowing the texture coordinates.
- **`ProgressBar`**: value 0 to 1, or indeterminate (a segment sliding with the surface clock; only an indeterminate bar asks for `Update`, so a still bar costs nothing). **`Separator`**: a line in the border color. **`Panel`**: a `StackLayout` drawing the `panel` style.
- **`Button`**: text and/or an icon; clicks when the left button is released inside it after being pressed inside (a drag out cancels), or on Space or Enter while focused; `OnClick()` is a `Callback<Button*>`; `SetDefault` marks the default button of a dialog. **`ToggleButton`**, **`CheckBox`** (optional tri-state), **`RadioButton`** inside a **`RadioGroup`** (one selected, arrow keys move the selection and the focus, only the selected one is a Tab stop).
- **`Callback<Args...>`** (`ui/callback.h`): function pointer plus user pointer, several listeners, `Connect` returns a token for `Disconnect`; a listener may disconnect another while it runs.
- Every widget has a `Name()`, an optional `Description()` and a `Tooltip()` text (the tooltip popup is Day 198).

```cpp
ui::Button* ok = AddButton(root, "ok", "OK");           // however your code makes widgets
ok->SetVariant("primary");
ok->OnClick().Connect([](void* user, ui::Button*) { static_cast<Dialog*>(user)->Accept(); }, this);
```

## Value widgets and scrolling: slider, spin box, scroll bar, scroll view, list box

- **`ValueRange`** (`ui/widgets/range.h`): `min`, `max`, `step`, `integer` and a `RangePolicy`. `Snap` puts a value on the grid as `min + k * step` with `k` rounded once (never by adding the step repeatedly, so 1,000 steps of 0.1 land exactly on the grid points); a maximum that is not on the grid is not reached, the last grid point below it is. NaN becomes `min`. `RangePolicy::Clamp` pulls an out-of-range value to the nearest end, `Reject` keeps the old value.
- **`Slider`**: horizontal (minimum left) or vertical (minimum at the bottom). A press on the track jumps to that point, or pages toward it with `SetTrackPages`; a press on the thumb keeps the grab offset; the pointer is captured while dragging. `OnChanged()` runs on every change, `OnCommit()` when a drag ends or a key changes the value. Keys: arrows, PageUp/PageDown (a tenth of the range), Home/End. The wheel is off until `SetWheel(true)`. `SetTicks(n)` draws marks.
- **`SpinBox`**: a number with up/down buttons, the wheel and the keys; `SetDecimals` formats the text. Typing arrives with the text field (Day 197).
- **`ScrollBar`**: the thumb is `track * view / content`, not smaller than the minimum hit size, and flush with the end at the maximum value; a content smaller than the view fills the track and the value stays 0. A track press pages by one view, a thumb drag is captured, arrows (`SetArrows`) move by `SetLineStep`. `SetAutoHide` draws the bar only while it is hovered, dragged or just changed.
- **`ScrollView`**: one content widget larger than the view, a vertical and a horizontal bar by policy (`Auto`, `Always`, `Never`), the offset clamped to the content. The wheel scrolls by a line step and is handled only when it moved, so an inner view at its end passes the wheel to the outer one. `ScrollTo(rect)` takes a rectangle in the coordinates the view's bounds are in and moves the least; a widget that takes the focus inside the content is scrolled into view. There is no scroll inertia.
- **`ListBox`**: items come from a `ListModel` (`ItemCount`, `ItemText`). Single or multiple selection (Ctrl toggles, Shift selects a range from the anchor, Shift+arrows extend, Ctrl+A selects all), arrow, page, Home/End keys, Enter activates, and typing a letter jumps to the next item starting with it (more letters within a second extend the prefix, the same letter again cycles). Only the rows in view are laid out and drawn, from a ring of cached text runs, so 100,000 items cost what 100 do. `ModelChanged()` after the model changes clears the selection and clamps the scroll.

Steady state: a frame that scrolls within rows already laid out allocates nothing (the surface keeps its canvas and event buffers between frames). A row that comes into view for the first time is laid out by the text module, which allocates.

```cpp
ui::ListBox* list = AddListBox(root, "files");   // however your code makes widgets
list->SetModel(&my_model);                        // a ui::ListModel the caller keeps alive
list->OnActivate().Connect([](void* user, ui::ListBox* l) { Open(static_cast<App*>(user), l->Current()); }, app);
```

## The text field

`TextField` (`ui/widgets/text_field.h`) edits one line of UTF-8 text. Offsets are byte offsets on grapheme boundaries (`text::NextBoundary`, `PreviousBoundary`, `OffsetAtPoint`, `PositionOfOffset` and `SelectionRects` over the field's layout), so a combining mark, a flag, a ZWJ sequence or a Hangul syllable moves, selects and deletes as one unit.

- **Keys:** Left/Right, Home/End (Shift extends the selection, Ctrl moves by word: `WordStart`/`WordEnd` in `ui/text_words.h`, whose classes are code point ranges, not a full Unicode table), Backspace/Delete (Ctrl: by word), Ctrl+A, Ctrl+C/X/V with Ctrl+Insert, Shift+Delete and Shift+Insert, Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y, Enter (`OnSubmit`), Escape (`OnCancel`). Only `Text` events insert; Tab, Up, Down and unknown keys are left to the surface and the parents. There is no input method and no complex-script shaping.
- **Mouse:** a click places the caret, a drag selects (the pointer is captured), Shift+click extends, a double click selects a word, a triple click all; the I-beam shows on hover. A drag past the edge scrolls as the caret moves (there is no auto-scroll while the pointer is still).
- **Display:** the text scrolls sideways to keep the caret in view with a margin; the selection comes from `SelectionRects`; the caret blinks from the surface clock and restarts on every input; an unfocused field draws no caret; the placeholder shows while the field is empty and unfocused.
- **Rules:** `SetMaxLength` counts clusters (a longer insertion is cut), `SetValidator` sees the whole proposed text and can refuse it, `SetReadOnly` keeps caret, selection and copy, `SetPassword` draws a bullet per cluster and refuses copy and cut. One path applies them to every edit.
- **Undo:** a bounded history (`kMaxUndo`); typing is one step per run of letters or of spaces, a run of Backspace or of Delete is one step, moving the caret ends a run; a new edit drops the redo history.
- **Clipboard:** `ui::Clipboard` (`ui/clipboard.h`) has `SetText`, `HasText`, `RequestPaste(function, user)` and `CancelPaste`. Paste is asynchronous in shape: the function runs before `RequestPaste` returns (the in-process `LocalClipboard`, which every surface starts with) or later (the OS clipboard of Day 200); the field cancels a pending request when it is destroyed. `Surface::SetClipboard` swaps the implementation. A pasted text is cut at its first line break.
- **`SpinBox`** embeds a field with a numeric validator: digits, a sign when the range has negatives, a point when it has decimals. Enter and losing the focus commit (a text that is not a number goes back to the value, a value the range refuses too), Escape reverts, the buttons and the wheel change the value and the text together.

```cpp
ui::TextField* name = AddTextField(root, "name");   // however your code makes widgets
name->SetPlaceholder("Your name");
name->SetMaxLength(32);
name->OnSubmit().Connect([](void* user, ui::TextField* f) { static_cast<Form*>(user)->Accept(f->Text()); }, form);
```

## Popups, menus, combo boxes and tooltips

**The popup layer.** `Surface::OpenPopup(widget, anchor, options)` adds a widget to the popup layer, sizes it by its own `Measure` (with the surface as the limit) and places it with `PlacePopup` (`ui/popup.h`): on the preferred side of the anchor (`Placement::Below`, `Above`, `Right`, `Left`) when it fits, on the opposite side when only that fits, then shifted along the anchor's edge and clamped so that it starts inside the surface. Popups form a stack:

- a press outside all of them closes them all, and is consumed (`consume_outside_click`, the default) or delivered as usual; a press on the popup's `owner` closes it and is always consumed, so a click on a combo box toggles its list;
- Escape that the focus owner did not take closes the top popup; losing the window focus closes every popup; closing a popup closes the ones above it;
- a popup takes the keyboard focus (`take_focus`), and the surface gives it back to the previous owner when the popup closes (to the popup below when one is under it);
- a popup whose `owner` is destroyed closes. Closing inside an event handler is deferred to the end of the delivery, like removal.

`Surface::ShowContextMenu(widget, position)` opens a popup at the pointer, moved to stay inside; `Widget::SetContextHandler(function, user)` calls a function on a right press (the nearest widget under the pointer that has one) so that it can open one.

**Accelerators.** `RegisterAccelerator(chord, function, user)` returns an id (0 when the chord is taken); `SetAcceleratorEnabled` turns it off, `UnregisterAccelerator` removes it. The order of a key press is: the focus owner and its parents, then Tab and Escape (popups), then the accelerators, then the menu bar's keys (F10, Alt+letter). So a text field gets Ctrl+Z before a global Ctrl+Z, and a widget that handles a key shadows an accelerator on it. `ChordText` gives "Ctrl+Shift+S" for a menu to show.

**Menus.** A `Menu` (`ui/widgets/menu.h`) is a model: items (`AddItem`, `AddCheck`, `AddRadio` with the run of neighbouring radio items exclusive, `AddSubmenu`, `AddSeparator`), each with an action `void(*)(void* user, usize index)`, an enabled flag and optionally the id of a surface accelerator whose chord shows at the right and whose off state dims the item. `&` before a letter marks the mnemonic (`&&` is a literal ampersand; `SplitMnemonic`). A `MenuView` shows a menu as a popup: Up and Down move over the enabled items and wrap, Home and End jump, Enter and Space choose, a mnemonic letter chooses, Right opens and enters a submenu, Left leaves it; the pointer highlights, a click chooses, and resting on a submenu item opens it after `kSubmenuDelay`. Choosing a leaf runs its action and closes every popup. The menu must outlive its views.

**The menu bar.** `MenuBar` is a row of titles. A click opens a menu below its title; with a menu open the pointer switches menus, and Left/Right in a menu that has no submenu to enter or leave switch to the neighbour. Install it with `Surface::SetMenuKeyTarget(bar)` and F10 or a lone Alt (pressed and released with no other key between) takes the focus and underlines the mnemonics, the arrows move, Down/Enter/Space open, a letter opens, Escape or F10 leaves; Alt+letter opens the menu from anywhere. The bar remembers who had the keyboard focus (`Surface::SaveFocus`) and gives it back when it is left or its menu closes (`RestoreSavedFocus`).

**Combo box.** `ComboBox` (`ui/widgets/combobox.h`) shows one item of a `ListModel` and drops a virtualized `ListBox` (at most `kDropRows` rows, at least as wide as the box). Closed: Up/Down/Home/End and type-ahead select (running `OnSelectionChanged`), the wheel when `SetWheelSelects(true)`; F4, Alt+Down, Space, Enter and a click open the list. Open: the list has the keyboard, Enter or a click on an item chooses it, Escape or a click elsewhere keeps the old selection. `SetEditable(true)` adds a `TextField`: typing text equal to an item selects it, any other text selects none, choosing an item replaces the text.

**Tooltips.** Any widget's `SetTooltip(text)` shows a bubble near the pointer once the pointer has rested on it for `Surface::kTooltipDelay` seconds of the surface clock (the delay counts from the last movement). A press, a key, a wheel turn, opening a popup or the pointer leaving hides it, and it stays hidden until the pointer moves; no tooltip shows while a popup is open. One bubble at a time, kept inside the surface.

```cpp
ui::Menu file;
file.AddItem("&New", &OnNew, app);
file.AddCheck("Show &grid", true, &OnGrid, app);
file.AddSeparator();
file.AddItem("E&xit", &OnExit, app);
ui::MenuBar* bar = AddMenuBar(root, "bar");          // however your code makes widgets
bar->AddMenu("&File", &file);
surface.SetMenuKeyTarget(bar);
```

## Tab views, splitters, windows and modal dialogs

**`TabView`** (`ui/widgets/tab_view.h`) shows tab headers over pages. `AddTab(title, page)` adds a ready page, `AddLazyTab(title, factory, user)` builds its page the first time the tab is selected (once). Only the selected page is shown; the others are collapsed, not destroyed, so a text field keeps what was typed. A click, Left/Right/Home/End on the focused view, and Ctrl+Tab / Ctrl+Shift+Tab / Ctrl+PageUp/PageDown (also from inside a page) select; those user actions run `OnSelectionChanged`, `Select` does not. `SetClosable(true)` gives new tabs a close button that asks `OnCloseRequested`; the program calls `CloseTab`, which destroys the page and moves the selection to the neighbour. Headers wider than the view show two arrows at the right end, the wheel over the headers scrolls them, and selecting a tab scrolls it into view.

**`Splitter`** (`ui/widgets/splitter.h`) lays panes along one axis (`Orientation::Horizontal`: left to right, vertical handles) with draggable handles `kHandle` wide. Every pane has a relative size (`AddPane(pane, weight)`) that survives resizing in proportion; `SetMinSize` keeps a pane from being dragged or squeezed below a size; a double click on a handle restores the starting sizes; a pane marked `SetCollapsible` collapses when a drag takes it below half its minimum and opens when a drag takes it back, and `SetPaneCollapsed` does it from code. The resize cursor shows over a handle. The layout is numbers: `Fractions` and `SetFractions` (plus `PaneCollapsed`) are what a layout file stores. Docking is not implemented (the splitter and the tab view are its building blocks, Phase 27).

**`WindowFrame` and `Dialog`** (`ui/widgets/window_frame.h`, `dialog.h`). A `WindowFrame` is a panel with a title bar (the `titlebar` style), content, an optional footer, and an optional close button. Dragging the title bar moves it, clamped to its parent's bounds (the surface, when it is in a layer); `SetResizable(true)` resizes from the edges and corners (diagonal and straight cursors) down to `SetMinSize`. Put a frame where its bounds are left alone (the root, a layer), not in a layout container. A `Dialog` is a frame with a button row: `AddButton(text, result, is_default)`; Enter (when the focus owner does not take it) presses the default button, Escape and the close button close with the cancel result, and `Close(result)` runs `OnClosed` and removes the dialog. A focused text field takes Enter for itself (its `OnSubmit`), so Enter does not reach the default button from a field.

**Modal dialogs.** `Surface::ShowModal(dialog)` puts the dialog above everything but the popups, centers it, gives its first Tab stop the focus and covers the rest with the `modaldim` shade. Under it nothing is hit or hovered (the shade takes the pointer), Tab stays inside, keys go to the dialog while the focus is outside it, and accelerators and the menu bar do not run. Dialogs stack (each covers the one below); `CloseModal(dialog)` closes it and the ones above, closes the popups, and gives the focus back to whoever had it when the dialog opened (nobody when that widget is gone). A dialog the program removes itself takes its shade with it. Popups opened from the dialog work as usual.

**Message boxes.** `ShowMessageBox(surface, title, text, buttons, function, user)` shows wrapped text with OK, OK/Cancel, Yes/No or Yes/No/Cancel and calls `function(user, result)` (`dialog_result::kOk`, `kCancel`, `kYes`, `kNo`) when it closes; the first button is the default, Escape gives the set's cancel result. `ShowConfirm(surface, title, text, confirm_text, function, user)` is a named default button plus Cancel (the settings screen's restart prompt). There are no file dialogs this phase.

```cpp
ui::Dialog* d = ui::ShowConfirm(&surface, "Restart", "The change needs a restart.", "Restart now", &OnAnswer, app);
// OnAnswer(app, ui::dialog_result::kOk) when Enter or the button is pressed.
```

## Drag and drop, and the clipboard

**The model** (`ui/drag_drop.h`). A `DragPayload` is a list of typed entries: text, a file list (absolute paths separated by `\n`), a custom format (a name and bytes), and a pointer entry that is valid only inside the process and never offered to the OS. It also carries the actions the source allows (copy, move, link: the platform layer's `os::DragAction`) and the action the user asks for (`Requested()`: move by default, Ctrl for copy, Ctrl+Shift for link, always within what is allowed). Text is made valid UTF-8 and every entry is capped at 16 MiB, so a hostile source cannot hand over more than a clipboard could.

**A source** starts a drag from a press-and-move: it waits until `Surface::PastDragSlop(press, now)` (the theme's `DragSlop` metric) and calls `Surface::BeginDrag(payload, visual, offset)` while it holds the pointer. The surface then owns the pointer: hover goes to drop targets only, the optional `visual` widget tree follows the pointer in the top layer, and the source gets `OnDragEnded(action)` at the end (`Nothing` means cancelled or refused, `Move` asks it to remove what it gave). **A target** overrides `CanAccept(payload, position)` (answer an action, usually `payload.Requested()`, or `Nothing`), and optionally `OnDragEnter/OnDragOver/OnDragLeave/OnDrop`. The surface asks the widget under the pointer and then its ancestors, so a container can accept for its children, and popups and modal dialogs receive drags like any widget. A widget the drag is over is marked `kStateDragOver` and drawn with the `droptarget` style over it (the `dropmarker` style is the insertion line). The cursor shows the action (`CursorForDragAction`: not-allowed, the four-way arrow for move, the hand for link). Escape cancels; so does a release over no target.

**While a drag rests**, `Widget::DragAutoScroll` lets a scroll view, list box or tab header scroll when the pointer is within `kDragScrollBand` of an edge (faster the deeper it is), and `Widget::OnDragHoverDelay` runs after `Surface::kDragHoverSeconds` (a tab view selects the tab under the pointer; a menu would open its submenu).

**Built in.** `ListBox::SetReorderable` drags the selected items to a new place (the model implements `ListModel::MoveItems`; the box shows an insertion line and selects the moved items; a drop on the selection itself does nothing), and `SetDragOut` offers the selected items as text. `TextField` accepts a text drop at the pointer (one undo step), drags its selection (a move within the field removes the original, Ctrl copies; a press on the selection that does not move is a click), and refuses both when read-only or a password field. `TabView::SetReorderable` drags headers (`OnTabMoved`). A file list dropped on nothing that accepts it reaches `Surface::OnFilesDropped` (the root's handler).

**Other programs.** `WindowAdapter::EnableDrops(window)` makes the window accept text and file drags. A drag from another program becomes an external drag on the surface: while it moves, a target sees entries that are `pending` (their kinds, no data); on the drop the adapter reads the data asynchronously and the target gets `OnDrop` only with complete data. A read that fails ends the drag and runs `Surface::OnDropFailed`, never a half drop. A drag started on the surface whose pointer leaves the window becomes an OS drag when it has any text, file or custom entry (`Window::BeginDrag`); the window's `DragEnded` event ends it for the source. A payload of pointers only stays in the process.

**The clipboard.** `ui::OsClipboard` (`ui_window/os_clipboard.h`) puts the OS clipboard behind the toolkit's `Clipboard` interface; give it to the surface with `Surface::SetClipboard` and to the adapter with `WindowAdapter::SetClipboard`. It is asynchronous: Paste in a text field requests the data and shows nothing until the window's `ClipboardData` event arrives (then inserts it as one undo step); a request that times out or finds nothing calls `OnFailed` and inserts nothing. `HasText` reads the window's record of the selection, which `ClipboardChanged` keeps, so a Paste command enables without asking. Offscreen surfaces and tests keep the in-process `LocalClipboard`. Where the platform has a primary selection (Linux), selecting text in a field fills it and a middle click pastes it at the pointer; elsewhere both are off.

**Edit commands.** `Widget::CanEdit(EditCommand)` and `RunEdit` let the Edit menu act on the focus owner: `AddEditItems(menu, surface)` adds Cut, Copy, Paste and Select All, `RefreshEditItems` enables them from the edit target (the focus owner, or the one a menu borrowed the focus from) when the menu is about to show. Ctrl+C/X/V, Ctrl+Insert, Shift+Insert and Shift+Delete go to the focus owner before the surface's accelerators.

Limitations: a drag from a list box to a list box (other than itself) is not built in (drop text into a field instead); a text field drops only the first line of a text; the copy action has no cursor of its own (the platform layer has no copy cursor); a menu does not yet open its submenu on a drag hover.

## The viewport

**The widget** (`ui/viewport.h`). `ui::Viewport` is a rectangle that shows a rendered world; it knows nothing of the GPU. The application implements `ViewportBinding`: `OnResize(width, height)`, `Render(frame)`, `Image()` (the `ImageId` the UI draws over the rectangle, 0 for none yet) and `OnInput(input)`. Every frame, after layout, the viewport works out its size in **physical pixels** (bounds times the surface scale), calls `OnResize` only when that size differs from the last one it reported (so a window dragged through ten sizes between two frames costs one call, with the final size), then `Render`, then draws the image. A zero-size viewport (a collapsed pane) calls nothing and the binding keeps its target; a hidden or collapsed one is not drawn at all, so it renders nothing and changes nothing. `AspectOf(width, height)` is the helper for the camera. Children of a viewport (a HUD label, a gizmo handle) draw after the image inside its clip and get the pointer before the viewport does.

**The target** (`renderer/viewport_target.h`). `ViewportTarget` owns the colour texture (and a depth texture if asked) a viewport's world is drawn into. A *render target* is a role a texture plays, not a kind of resource: the same texture is a colour attachment while the world is drawn and a sampled texture while the UI pass reads it. It is an `ImageSource`, so `UiRenderer::SetImage(id, &target)` makes it drawable. There are two ways to draw the world into it. In **the same frame graph** as the UI: `ImportColor(graph)` brings the texture in, the world's last pass (the tonemap) writes it, and `UiRenderer::AddToGraph(..., sampled)` is given the same resource so the graph orders the UI pass after the writers. In **a frame of its own**: `Renderer::RenderFrame(scene, camera, &target.AsFrameTarget())` draws the Phase 5 world into it and leaves it ready to sample; the UI frame follows.

`Resize` makes the new textures first and destroys the old ones after (the device keeps a destroyed texture until the GPU is done with it, so a frame in flight is safe); the same size does nothing. The target registers with `DeviceResources` at the textures tier: a device reset releases its textures and recreates them at the same size, empty until the next frame draws the world (the UI skips an image whose texture is missing), and a `Resize` between the release and the recreate only records the size. The registry asks `Live()` when an owner registers, so a target sets its members before it attaches (a target that did not was left out of the reset and leaked its texture; the lit-cube test checks `DeviceHost::LeakedObjects()`).

**Input.** `ViewportInput` carries positions in the viewport's own pixels (not logical units; `scale` says how many pixels make a logical unit): `PointerMove`, `PointerDown/Up`, `Wheel`, `Enter/Leave`, `KeyDown/Up` and `FocusGained/Lost`. A click focuses the viewport (Tab walks past it) and the capture is held while a button is down, so drags that leave the rectangle keep arriving. The binding sets `handled` to take a key or wheel event; pointer events are always taken.

**Mouse look.** `Viewport::SetLookButton` names the button (the right one by default; 0 turns it off). Pressing it sends `LookBegin` and asks the surface for a pointer lock: `Surface::RequestPointerLock` goes to the `PointerLockHost` the adapter provides (`WindowPointerLock` over `os::Window::SetPointerLock`), and while the lock holds, the window's `RelativeMotion` events reach the binding as `Look` deltas in device units (`locked` is true). Where the pointer cannot be locked the viewport falls back to captured absolute motion, as the difference between positions in pixels, and logs a warning once per viewport. Releasing the button, losing the keyboard focus, the window losing focus, or the viewport being hidden or destroyed ends the look and frees the lock (`LookEnd`; a hidden viewport sees it at the next frame it is shown).

**Several viewports** in one surface each have their own binding, focus and capture, render in tree order, and only one holds the pointer lock at a time.

## Surfaces in the world: render to texture, update policies and input without a window

**A texture for the world.** A `ui::Surface` does not need a window. `renderer::UiSurfaceTarget` (`renderer/ui_surface_target.h`) owns the colour texture a surface is drawn into, so a material in the world (a screen on a wall, a monitor) simply samples a texture and knows nothing of the UI. Per frame: `BeginFrame()`, then `AddToGraph(graph, ui_renderer, list)` when the surface was rebuilt (or `NeedsPaint()`), which imports the texture, clears it to the background colour and draws the list over it; then `Sampled(graph)` hands the same graph resource to the pass that reads it (`Read(resource, GraphUsage::Sampled)`), so the graph orders the reader after the paint. A frame that did not paint calls only `Sampled`, which imports the texture as it was left: **an idle surface adds no pass and no GPU work**. Use one `UiRenderer` per surface, with the target's format. The window's own UI pass is declared last and can sample the surface too (`UiRenderer::AddToGraph(..., sampled)`). Mips are not generated; the renderer's texture rules apply.

**Settings per surface.** The surface has its own size, scale and theme (`Surface::Resize`, `SetTheme`); the target has its own size (`Resize`), format and background. An sRGB format (the default) makes the UI show the colours the theme says when the material samples it as sRGB: the clear colour is decoded for the hardware to encode, and the UI pass already does the same for vertex colours. A UNORM format stores the colours as they are. A background with alpha 0 makes a transparent overlay; how it is blended over the world is the material's choice. A resize, a changed background or a device reset empties the texture: `NeedsPaint()` is true until the next `AddToGraph`, whether or not the surface changed (the draw list is kept, so repainting does not rebuild it).

**Update policies** (`ui/update_policy.h`). `SurfaceUpdater` applies an `UpdatePolicy` to one surface: `EveryFrame`, `OnDemand` (a frame runs only when `Surface::NeedsFrame()` says so: the tree is dirty, events are queued, a drag is going, a tooltip is waiting, or a shown widget asks for `Update`, which covers animations and a focused text field's caret) or `FixedRate` (at most N frames a second). The first on-demand frame always runs. The time of skipped frames is added to the next frame that runs, so animations keep their speed. `Frame` returns whether a frame ran and whether the list was rebuilt: paint the texture when it was.

**Input without a window.** `SurfaceInput` (`ui/surface_input.h`) posts to a surface the events a window adapter would, from pointer positions in the surface's own pixels or as a point (u, v) of the surface. A surface is display-only until `SetInteractive(true)`. `Lost()` releases the buttons still held and clears the hover, which is what the application calls when the ray stops hitting the screen. `RayToSurfacePoint(ray, quad)` is the math for the application's ray cast: no scene types, the quad is a corner and two edge vectors, and the result is the hit as fractions along the edges (`u` to the right, `v` down), the distance, and whether the ray came from behind (`back_face`, not a hit). Feed the result to `PointerMoveUv` and `PointerButtonUv`.

## The settings screen and the debug overlay

**A settings screen from the registry** (`ui/settings_view.h`). `SettingsView` is a column of rows, one per setting whose key starts with the prefix you give, in key order, under a heading per `SettingDesc::group` ("General" when there is none). The widget follows the type: a bool is a check box; an int or float is a slider and a spin box that always show the same value; an enum is a combo box over its names; text is a text field, a password field when the setting is `kSecret`. Ranges and steps come from the declaration. `kHidden` settings get no row; `kAdvanced` ones sit in rows collapsed until the "Show advanced settings" box is ticked; a `kRestartRequired` one has a "restart required" note, and `RestartPending()` and `OnRestartNeeded()` tell the program to offer a restart.

A change is written with `Settings::Apply`, passing the apply function given to `SetApply` (for the renderer, the glue to `Renderer::ApplySettings`). If the setting refuses the value, or the apply function returns false, the widget returns to the value in force and `Message()` says why. A value that the environment, the command line or a policy layer fixes is shown disabled, with the source as its tooltip (`Settings::Provenance`). The rows are refreshed from a settings listener, never by polling: the listener only raises a flag (it runs on whichever thread changed the setting), the view asks for `Update` while the flag is up, and an idle screen reads nothing from the settings (`Refreshes()` counts the reads; the test checks that 100 idle frames add none). The listener's prefix is the view's prefix without the closing dot.

**The debug overlay** (`ui/debug_overlay.h`). `DebugOverlay::Attach(surface, overlay, chord)` puts a panel at the top right of the surface and registers the key (for example the grave accent) that shows and hides it. Hidden, it is collapsed: no layout, no draw and no `Update`. It holds a `FrameGraph` (a bar per frame time, the last 120), a line of counters, a log console and a command line. The program feeds it plain numbers each frame with `SetStats(DebugStats)` (frame and GPU time, draw calls, jobs, GPU memory): the overlay reaches into no other module.

**The log console.** `LogCapture` copies the log core's lines on the UI thread. The log core already keeps the last 256 lines in a lock-free ring that any thread writes, so the capture only *reads* it, with `ReadLogLinesSince(next, callback, context)`: it delivers the lines logged since the number the last call returned (the oldest still held, when the ring wrapped), so there is no sink to register and no UI call from another thread. The capture keeps the newest 512 lines and a text filter (a level name such as `WARN` works, as the lines carry it). Only lines that pass the log channel's own verbosity threshold are in the ring.

**Commands.** The command line runs on Enter: the first word names a command added with `AddCommand(name, help, function, user)`, the rest of the line is its argument, and the function writes its output into the console through the `LogCapture` it receives. `help` lists the commands, `clear` empties the console, an unknown name says so.

More sections arrive with the days that build them (the demo application).
