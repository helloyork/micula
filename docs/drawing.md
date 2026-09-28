# Drawing

`PaintPage` and `Widget::Paint` receive a `Painter`. Colors come from the window's
`Palette`, text formats from its `Fonts`. Everything here is in DIPs.

## Painter

`struct micula::Painter`, in `micula/window.h`.

| Member | Description |
|---|---|
| `ID2D1DeviceContext *rt` | The Direct2D context, for anything the methods below don't cover. |
| `const Palette *pal` | The window's colors. |
| `const Fonts *font` | The window's text formats. |
| `ID2D1Brush *Brush(const D2D1_COLOR_F &c) const` | A solid brush set to `c`. One brush is reused, so use it before asking for another color. |
| `void Fill(const D2D1_RECT_F &r, const D2D1_COLOR_F &c) const` | Fills a rectangle. |
| `void FillRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &c) const` | Fills a rounded rectangle. |
| `void StrokeRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &c, float width = 1) const` | Strokes a rounded rectangle inside `r`, so a 1-DIP border is sharp. The radius comes in with it -- an 8-DIP corner inset by 0.5 is a 7.5-DIP corner -- because outer radius minus the margin is the inner radius, and anything else leaves the corner not quite round where the stroke meets it. |
| `void Panel(const D2D1_RECT_F &r, const Corners &c, const D2D1_COLOR_F &fill, const D2D1_COLOR_F &border = transparent, unsigned edges = edge::kAll, float width = 1) const` | Fills a panel whose four corners are chosen one by one, and strokes `border` inside its edge. `edges` is which edges the border runs along -- a corner needs both of the edges meeting there -- and a transparent border or `edges = 0` draws none. |
| `void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F &c, float width = 1) const` | Draws a line. |
| `void Shadow(const D2D1_RECT_F &r, float radius, float opacity = 1, float reach = 14, float drop = 4, int layers = 12, float strength = 0.11) const` | The shadow a surface over the page casts -- a flyout, a dialog, a menu, a pane arriving. Fluent's is a blur and Direct2D has none worth affording per frame, so this is `layers` rounded rectangles, the largest and faintest outermost, whose weights fall off quadratically so that the outside fades into the surface rather than ending on a step. What lands on the surface is `strength`, whatever `layers` is. `reach` is how far it spreads and `drop` how far it sits below the surface, so that the light reads as being above. **What reads as height is the spread, not the darkness**: over a dim, a merely darker shadow has nowhere left to be darker *than*. Fluent's ambient shadow is a blur of 8 with no offset at 20 per cent, and on Windows the sharp half of the pair is the 1-DIP stroke every elevation has instead -- so a flyout wants a narrow shadow and no contour, `p.Shadow(r, 8.0f, openF, 14.0f, 4.0f)`, and a dialog a tighter one plus a contour somebody can see, `p.Shadow(r, radius, 1.0f, 12.0f, 2.0f, 14, 0.18f)`. `opacity` fades it with whatever the caller is animating: a surface that fades while its shadow stays is the one thing that gives a fade away. |
| `void Text(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt, const D2D1_COLOR_F &c) const` | One line of text, left-aligned, vertically centered in `r` and clipped to it. |
| `float TextWrapped(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt, const D2D1_COLOR_F &c, bool measureOnly = false, bool clip = false) const` | Wrapped text from the top of `r`. Returns its height. `measureOnly` measures without drawing; `clip` cuts it off at the bottom of `r`. |
| `float TextWrappedCentred(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt, const D2D1_COLOR_F &c) const` | The same paragraph, centred: line by line in `r`, and as a block in it, clipped to `r`. Returns its height. For the one line a panel has to say in the middle of a lot of nothing, which the above cannot say -- the alignment of the lines belongs to the text format, and the format is shared. |
| `float MeasureWidth(const std::wstring &s, IDWriteTextFormat *fmt) const` | Width of one line of text, in DIPs. Measured once per string and format and kept in `Fonts`, so asking again is a lookup rather than a DirectWrite layout: a control that centres its label by measuring can do it in `Paint` every frame. |

To measure outside of painting, in `Layout()`:

```cpp
micula::Painter measure;
measure.font = &fonts;
const float w = measure.MeasureWidth(L"Label", fonts.body);
```

### Geometry

| Function | Description |
|---|---|
| `D2D1_RECT_F Rect(float x, float y, float w, float h)` | A rectangle from position and size. |
| `float Width(const D2D1_RECT_F &r)`, `float Height(const D2D1_RECT_F &r)` | Size of a rectangle. |
| `bool Inside(const D2D1_RECT_F &r, float x, float y)` | Whether a point is in a rectangle. |

## Palette

`struct micula::Palette`, in `micula/theme.h`. `Window::pal` holds the current one.
Most colors are translucent, like the Fluent tokens they come from, so Mica shows
through them.

| Field | Fluent token | Use |
|---|---|---|
| `dark` | | True for the dark theme. |
| `accent` | system accent | Accent fills: primary button, checked box, selection. The shade Windows uses for this theme. |
| `accentHover`, `accentPressed` | | `accent` under the pointer and pressed. |
| `accentText` | | Text on `accent`. |
| `windowBg` | SolidBackgroundFillColorBase | Opaque page background, used only without Mica. |
| `layerBg` | LayerFillColorDefault | A large area of the page over the backdrop. |
| `cardBg`, `cardStroke` | CardBackgroundFillColorDefault, CardStrokeColorDefault | Cards. |
| `flyoutBg`, `flyoutStroke` | SolidBackgroundFillColorTertiary, SurfaceStrokeColorFlyout | Surfaces over the page, such as an open drop-down. Opaque. |
| `controlBg`, `controlBgHover`, `controlBgPressed` | ControlFillColorDefault, Secondary, Tertiary | Control backgrounds by state. |
| `controlBgInput` | ControlFillColorInputActive | A text field with the caret in it. |
| `controlStroke` | ControlStrokeColorDefault | Control borders. |
| `controlStrokeBottom` | ControlStrokeColorSecondary | The darker bottom edge of a button or field. |
| `subtleHover` | SubtleFillColorSecondary | Hover fill of a borderless element. |
| `controlStrong` | ControlStrongFillColorDefault | Scroll bar thumb and arrows. |
| `acrylicInApp` | AcrylicInAppFillColorDefault (fallback) | Scroll bar track. |
| `textPrimary`, `textSecondary`, `textDisabled` | TextFillColorPrimary, Secondary, Disabled | Text. |
| `ok`, `warn`, `bad` | | Status colors for text and icons. |
| `okBg`, `warnBg`, `badBg` | | Status backgrounds. |

### Colors

| Function | Description |
|---|---|
| `Palette MakePalette(bool dark)` | The palette for a theme, with the system accent. |
| `D2D1_COLOR_F Rgb(UINT32 hex, float alpha = 1)` | A color from `0xRRGGBB`. |
| `D2D1_COLOR_F Mix(const D2D1_COLOR_F &a, const D2D1_COLOR_F &b, float t)` | `a` at `t = 0`, `b` at `t = 1`, alpha included. |
| `D2D1_COLOR_F Fade(const D2D1_COLOR_F &c, float mul)` | `c` with its alpha multiplied by `mul`. |
| `D2D1_COLOR_F Shade(const D2D1_COLOR_F &c, float t)` | Towards white for positive `t`, towards black for negative. |

## Fonts

`struct micula::Fonts`, in `micula/theme.h`. `Window::fonts` holds the window's.

| Field | Size | Face |
|---|---|---|
| `title` | 28 | Segoe UI Variable Display Semibold |
| `subtitle` | 20 | Segoe UI Variable Display Semibold |
| `bodyStrong` | 14 | Segoe UI Variable Text Semibold |
| `body` | 14 | Segoe UI Variable Text |
| `caption` | 12 | Segoe UI Variable Text |
| `mono` | 12 | Cascadia Mono, or Consolas |
| `icon` | 16 | Segoe Fluent Icons |
| `iconLarge` | 28 | Segoe Fluent Icons |
| `iconSmall` | 10 | Segoe Fluent Icons |
| `iconTiny` | 8 | Segoe Fluent Icons |
| `IDWriteFactory *dw` | | The DirectWrite factory. |

Where Segoe UI Variable is missing (Windows 10) the text faces fall back to Segoe UI.
The icon faces fall back to Segoe MDL2 Assets. All formats are single-line and
vertically centered; `Painter::TextWrapped` makes a layout that wraps.

| Function | Description |
|---|---|
| `IDWriteTextFormat *MakeFormat(IDWriteFactory *dw, const wchar_t *family, DWRITE_FONT_WEIGHT weight, float size)` | Another format like the ones above. The caller releases it. |
| `bool HasFamily(IDWriteFactory *dw, const wchar_t *family)` | Whether a font family is installed. |

## Metrics

`namespace micula::metric`, in DIPs.

| Constant | Value | Use |
|---|---|---|
| `kControlH` | 32 | Height of buttons, fields, drop-downs. |
| `kRadiusControl` | 4 | Corner radius of controls. |
| `kRadiusCard` | 8 | Corner radius of cards and flyouts. |
| `kTextLift` | 1 | How far a label drawn beside an icon is raised |
| `kButtonMinW` | 100 | Minimum button width. |

Text is centred in its rectangle by its *line box*, and a line box is not the ink: the ascender
above the cap band is taller than the descender below the baseline, so a label reads low beside
a glyph whose ink is centred in its own em -- which is every icon. Lift the text by `kTextLift`
to put the two back on one line. Both are measured: an unraised label sits 0.7 to 1.7 DIP below
the icon, depending on whether it has a descender in it.

## Corners

`struct Corners`, in DIPs, is a panel's four corner radii in XAML's order -- top-left,
top-right, bottom-right, bottom-left. 0 is a square corner; anything can be mixed, and
the four may differ. A `Painter::Panel` takes one, and `namespace edge` says which of the
four edges its border runs along:

```cpp
// The content layer of a page: its top-left corner rounded, the other three square, and
// a border along the two edges that face the rest of the window.
p.Panel(frame, Corners(metric::kRadiusCard, 0, 0, 0), c.layerBg, c.cardStroke,
        edge::kTop | edge::kLeft);
```

Draw the whole shape in one call rather than assembling it from overlapping fills. A
translucent colour -- and a layer colour is one -- lays down a second coat wherever two
fills cross, so a rounded rectangle with squares patched onto its corners leaves a band
of a lighter colour down those edges.

## Icons

`namespace micula::glyph`, in `micula/glyphs.h`: Segoe Fluent Icons code points as
`const wchar_t *`, named after the icon each one is. Draw them with `fonts.icon` (or another
icon size), never with a text format -- that is the icon font's own format, and a text format
draws the substitution box instead. Each also exists in Segoe MDL2 Assets, which is what makes
the font fallback safe on Windows 10.

The header is the list, and it is the one header of the library a page is expected to add to:
an icon that is not in it belongs there, beside the others, with the icon's own name in a
trailing comment. The font has well over a thousand; the header carries the ones a desktop
page reaches for.

## Motion

`namespace micula::motion`, in `micula/theme.h`. Durations and curves from WinUI.

Every follower here is **put on its target instead of moving towards it** when animations are
off -- see [Animations](#animations) below. Nothing that uses one has to ask, and nothing is
left standing half way when the switch moves.

| Name | Description |
|---|---|
| `kFaster` | 0.083 s. Pointer states. |
| `kFast` | 0.167 s. A control taking a new value. |
| `kNormal` | 0.250 s. Panels, flyouts, pages. |
| `float Decel(float u)` | Ease-out for things arriving: cubic-bezier(0, 0, 0, 1). `u` from 0 to 1. |
| `float Accel(float u)` | Ease-in for things leaving: cubic-bezier(1, 0, 1, 1). |
| `float InOut(float u)` | Ease in and out, for something crossing a track: cubic-bezier(0.4, 0, 0.6, 1), the curve `KeySpline="0.4, 0.0, 0.6, 1.0"` draws. Solved numerically, unlike the two above. |
| `bool Ramp(float *now, float want, float dt, float seconds)` | Moves `*now` linearly towards `want`, covering 0 to 1 in `seconds`. Returns true while still moving. For color fades. |
| `bool Follow(float &at, float to, float dt, float lag, float snap)` | Exponential approach to `to`, time constant `lag` seconds, arriving once within `snap`. Retargetable at any point. Returns true while still moving. For the wheel, the scroll bar, a page's glide -- anything whose target moves several times inside one frame. |

`struct Track` animates one value along a curve:

| Member | Description |
|---|---|
| `Track(float at = 0)` | Starts at rest at `at`. |
| `float value` | The value to draw. |
| `void To(float target)` | Starts moving from the current value to `target`. Does nothing if `target` is already the target. |
| `void Set(float at)` | Jumps to `at` without animating. |
| `bool Step(float dt, float seconds, float (*ease)(float) = Decel)` | Advances by `dt`. Returns true while moving. |
| `bool Moving() const` | True until the current movement ends. |
| `bool Wants(float target) const` | True while moving, or while `target` differs from the current target. Use this in `Widget::Animating()`. |

`struct Span` moves a marker between slots and stretches the interval it is drawn across:

| Member | Description |
|---|---|
| `Span(float at = 0)` | Starts at rest on slot `at`. |
| `float lead`, `float trail` | The two edges, in slots. Equal at rest. |
| `float slide`, `float close` | Time constants, in seconds: 0.05 for the leading edge and 0.028 for the trailing one. Equal values weld the edges together. |
| `void To(float at)` | The slot it belongs on. Aimed, not started: call it every frame. |
| `void Set(float at)` | Jumps to `at` without animating. For a first layout. |
| `bool Step(float dt)` | Advances by `dt`. Returns true while moving. |
| `bool Wants(float at) const` | Anything left to do to reach `at`. Use this in `Animating()` and `AnimationWanted()`. |
| `float Lo() const`, `float Hi() const` | The two edges, the lower one first: draw between them, in DIPs of one slot's pitch. |

## Animations

In `micula/theme.h`. Whether the library animates at all.

Three states, because a program's own answer and the machine's are different things and each has
to survive the other: `Auto` follows the machine, `On` and `Off` override it. `Auto` is the
default.

| Function | Description |
|---|---|
| `bool Animations()` | True while the library animates. What every follower in `motion` asks, and what a page with an animation of its own should ask. |
| `void Animations(bool on)` | This program's answer, which the system setting does not override. |
| `void Animations(AnimationMode mode)` | The same, as one of the three states. |
| `void AnimationsAuto()` | Follow the machine again. |
| `AnimationMode AnimationsSetting()` | Which of the three it is in. |
| `void RefreshAnimations()` | Re-read the machine. The window calls it on `WM_SETTINGCHANGE`, so a setting that moves while the window is open is picked up. |

The machine's answer is `SPI_GETCLIENTAREAANIMATION`: Settings > Accessibility > Visual effects >
Animation effects, which is the switch `UISettings.AnimationsEnabled` reports. There is no second
setting for smooth scrolling -- scrolling that is smooth *is* scrolling that is animated, and this
is what it answers to, which is also how WinUI treats it. (The "smooth-scroll list boxes"
checkbox in the same dialog belongs to the Win32 list box and is not asked here.)

Turning animations off does not freeze what is in flight: the next frame puts every follower on
its target. What is not an animation keeps running -- the caret blinks, a scroll bar's auto-hide
comes out and goes away at once instead of fading, and a progress bar's phase is still read from
the clock.

## System

In `micula/theme.h`.

| Function | Description |
|---|---|
| `bool SystemUsesDarkTheme()` | The apps theme setting (AppsUseLightTheme). |
| `D2D1_COLOR_F SystemAccent(bool dark)` | The accent shade Windows uses in that theme. `#0078D4` if unavailable. |
| `bool SystemAutoHidesScrollBars()` | False when "Always show scrollbars" is on. |
