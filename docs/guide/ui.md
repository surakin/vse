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

More sections arrive with the days that build them (drag and drop, viewport, surfaces).
