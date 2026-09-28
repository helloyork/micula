// Micula / micula.h
//
// The one header to include. Micula is header-only: theme.h holds the palette, the
// motion curves and the fonts; window.h the composed window, the painter and the Widget
// base; widgets.h the controls. Each includes the one before it, so this is widgets.h
// under a name that will not change if the files are ever split differently.
//
// What the program has to provide, and nothing else:
//
//   - link    d2d1 d3d11 dxgi dcomp dwrite dwmapi imm32 windowscodecs user32 gdi32
//             ole32 oleaut32 advapi32. window.h names every one with #pragma comment,
//             so MSVC needs none of them on its command line; other toolchains do.
//             UIAutomationCore is not in the list because it is not linked: the four
//             functions used out of it are looked up at run time, so a program that
//             never sees a screen reader has no dependency on it at all.
//   - nothing else. Window::Create opens the COM apartment a window needs (WIC for the
//             caption icon and Window::Image, and UI Automation) on the thread that makes
//             it, and sets per-monitor v2 -- but only for a process that has not said
//             anything about DPI yet, so a manifest, or EnablePerMonitorDpi() by the
//             program, wins. A program that uses COM itself still initialises it: the
//             apartment is then the program's and the library leaves it alone.

#pragma once

#include "widgets.h"
