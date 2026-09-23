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
| `void StrokeRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &c, float width = 1) const` | Strokes a rounded rectangle inside `r`, so a 1-DIP border is sharp. |
| `void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F &c, float width = 1) const` | Draws a line. |
| `void Text(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt, const D2D1_COLOR_F &c) const` | One line of text, left-aligned, vertically centered in `r` and clipped to it. |
| `float TextWrapped(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt, const D2D1_COLOR_F &c, bool measureOnly = false, bool clip = false) const` | Wrapped text from the top of `r`. Returns its height. `measureOnly` measures without drawing; `clip` cuts it off at the bottom of `r`. |
| `float MeasureWidth(const std::wstring &s, IDWriteTextFormat *fmt) const` | Width of one line of text. |

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
| `kButtonMinW` | 100 | Minimum button width. |

## Icons

`namespace micula::glyph`: Segoe Fluent Icons code points as `const wchar_t *`. Draw them
with `fonts.icon` (or another icon size), never with a text format. Each also exists in
Segoe MDL2 Assets.

| Constant | Icon |
|---|---|
| `kCheck` | CheckMark |
| `kWarning` | Warning |
| `kError` | ErrorBadge |
| `kInfo` | Info |
| `kFolder` | FolderOpen |
| `kChevron` | ChevronDown |
| `kMenu` | GlobalNavButton, the pane toggle in a `SideNav` |
| `kRefresh` | Refresh |
| `kSettings` | Settings |
| `kShield` | Shield |
| `kBusy` | SyncStatus |
| `kCaretUp`, `kCaretDown` | CaretUpSolid8, CaretDownSolid8 |

Other icons can be written as escapes, for example `L"\uE74D"` for Delete.

## Motion

`namespace micula::motion`, in `micula/theme.h`. Durations and curves from WinUI.

| Name | Description |
|---|---|
| `kFaster` | 0.083 s. Pointer states. |
| `kFast` | 0.167 s. A control taking a new value. |
| `kNormal` | 0.250 s. Panels, flyouts, pages. |
| `float Decel(float u)` | Ease-out for things arriving: cubic-bezier(0, 0, 0, 1). `u` from 0 to 1. |
| `float Accel(float u)` | Ease-in for things leaving: cubic-bezier(1, 0, 1, 1). |
| `float InOut(float u)` | Ease in and out, for something crossing a track: cubic-bezier(0.4, 0, 0.6, 1), the curve `KeySpline="0.4, 0.0, 0.6, 1.0"` draws. Solved numerically, unlike the two above. |
| `bool Ramp(float *now, float want, float dt, float seconds)` | Moves `*now` linearly towards `want`, covering 0 to 1 in `seconds`. Returns true while still moving. For color fades. |

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

## System

In `micula/theme.h`.

| Function | Description |
|---|---|
| `bool SystemUsesDarkTheme()` | The apps theme setting (AppsUseLightTheme). |
| `D2D1_COLOR_F SystemAccent(bool dark)` | The accent shade Windows uses in that theme. `#0078D4` if unavailable. |
| `bool SystemAutoHidesScrollBars()` | False when "Always show scrollbars" is on. |
