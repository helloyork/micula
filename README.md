# Micula

Fluent-style controls for Win32 programs. Header-only C++17, with no dependencies
outside the Windows SDK.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/screenshot-dark.png">
  <img src="docs/screenshot-light.png" width="862" alt="A settings window built with Micula, with Mica behind it">
</picture>

Micula renders with Direct2D into a DirectComposition swap chain, so Windows 11 can draw
Mica behind the window. It draws its own title bar and keeps snap layouts working. On
Windows 10, and on Windows 11 before 22H2, the window gets a solid background instead.

It is meant for small tools: settings windows, installers, configuration dialogs. The
library adds about 85 KB to an x64 executable, and a program built with `/MT` runs on a
machine with nothing installed.

## Controls

Button, CheckBox, ToggleSwitch, Segmented, Slider, ScrollBar, DropDown, TextBox,
ProgressBar.

Colors, sizes and animation timings come from WinUI's control templates and the Fluent
design tokens. The accent color and the light or dark theme are read from the system.

## Example

```cpp
#include <micula/micula.h>

struct Hello : micula::Window {
    bool on = false;

    const wchar_t *ClassName() const override { return L"Hello"; }
    const wchar_t *Title() const override { return L"Hello"; }

    void Layout() override {
        ClearWidgets();
        auto *sw = Add(new micula::ToggleSwitch(L"Enabled", on, [this](bool v) { on = v; }));
        sw->rect = micula::Rect(24, 56, 280, 32);
        auto *ok = Add(new micula::Button(L"Close", micula::ButtonStyle::Accent,
                                          [this] { PostMessageW(hwnd, WM_CLOSE, 0, 0); }));
        ok->rect = micula::Rect(24, 104, 120, 32);
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    micula::EnablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Hello w;
    return w.Create(340, 170, false, nullptr) ? w.Run() : 1;
}
```

`examples/settings` is the window in the screenshot. `examples/gallery` puts every
control on one scrolling page.

## Building

The headers link the libraries they need through `#pragma comment`, so with MSVC this is
enough:

```bat
cl /std:c++17 /EHsc /O1 /MT /DUNICODE /D_UNICODE /I include app.cpp /link /SUBSYSTEM:WINDOWS
```

With CMake, add this directory and link `micula::micula`. Building the repository itself
builds both examples.

A program using Micula has to:

- compile with `UNICODE` and `_UNICODE` defined. The headers call the wide API, and a
  program that does not define them still gets the A variants of everything in
  `<windows.h>` -- including the names it hands this window, which are wide strings
  here. The CMake target defines both for you.
- initialize COM on the UI thread (apartment-threaded) before `Window::Create`. The title
  bar icon and `Window::Image` use WIC.
- be per-monitor DPI aware, through its manifest or `micula::EnablePerMonitorDpi()`.
- leave timer IDs 2 to 7 to Micula.

## How it works

Each window is a subclass of `micula::Window`. `Layout()` deletes the controls and
creates them again from the window's own fields, so a callback should update those
fields rather than the control. `Layout()` may be called from inside a callback. Text and
backgrounds that are not controls are drawn in `PaintPage()`. Coordinates are in DIPs.

For a scrolling page, return the scrolling area from `ClipRect()`, set `scrolls` on the
controls inside it, and move them by returning an offset from `ContentTransform()` -- so a
wheel notch costs a transform and a repaint rather than a `Layout()`, and the page is not
rebuilt as it scrolls. `examples/gallery` does this.

## Documentation

- [Overview](docs/README.md): headers and core concepts
- [Window](docs/window.md): creating a window, page callbacks, input, scrolling
- [Controls](docs/controls.md): the nine controls
- [Custom controls](docs/widget.md): writing a control of your own
- [Drawing](docs/drawing.md): painter, palette, fonts, icons, animation

## License

[MIT License](LICENSE). Micula is not affiliated with Microsoft.
