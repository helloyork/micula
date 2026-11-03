#pragma once

// The Segoe Fluent Icons code points, named so that a reader does not have to look them up.
// The font holds well over a thousand of them; this file carries the ones a desktop page
// reaches for, gathered loosely by what they are for -- the groups are a reading aid and
// nothing more, and each icon reads for whatever a page wants it to.
//
// Escapes rather than the characters themselves, so this header means the same thing whatever
// code page the compiler reads it in.
//
// A header of its own rather than a corner of the theme, because this is the one part of the
// library that is a list, and the one part a page is expected to add to: an icon that is not
// here belongs here, beside the others. Draw one through an `Icon` and never with a text
// format -- that is the icon font's own format, and a text format renders the substitution box
// instead.
//
// **A code point is a `Glyph`, and not a string.** It is still one pointer, `sizeof` and all, so
// nothing here got heavier -- but it no longer converts *from* one, and that is what stops an
// icon from being mistaken for words. A constructor that takes an icon can no longer be handed
// the first of two strings: `Card(L"a", L"b")` is the card without an icon, and a page has to
// write `Glyph(L"\ue713")` when it means an icon. `Icon` is the thing a control stores, and the
// thing a page subclasses when it has an icon of its own to draw.
//
// **Two classes of icon, and the second is a question about the running machine.** Most of the
// code points below are in Segoe MDL2 Assets at the same value as well, which is what makes the
// fallback in the theme's font setup safe: Windows 10's font draws the same icon. A few are not --
// Fluent Icons added them -- and a font without them draws the substitution box instead.
//
// **That cannot be decided at compile time.** The font is not shipped with the program: one built
// on Windows 11 runs on Windows 10, where the glyph is not there, and one built on Windows 10 runs
// on Windows 11, where it is. So the question is asked at run time -- `HasFluentIcons()` and
// `HasGlyph()`, in theme.h -- and `MICULA_IS_WIN11` is the short spelling of the first for a page
// that only wants to write its fallback once:
//
//     card->icon = MICULA_IS_WIN11 ? glyph::animated::Bars : glyph::kBug;
namespace micula {

// One code point of the icon font, which is all an icon is: a unit of Segoe Fluent Icons'
// private-use area, drawn with an icon format rather than with a text format.
//
// **The type is the point, not the storage.** It holds the same pointer the constants used to
// be, and the one conversion it refuses -- from a bare `const wchar_t *` -- is the whole reason
// it exists. Reading the code point back out is `Code()`, named so that the places that turn a
// glyph back into a drawing are visible at the call site.
struct Glyph {
    const wchar_t *codePoint = nullptr;
    constexpr Glyph() = default;
    constexpr explicit Glyph(const wchar_t *cp) : codePoint(cp) {}
    constexpr const wchar_t *Code() const { return codePoint; }
    constexpr bool empty() const { return !codePoint || !*codePoint; }
    constexpr explicit operator bool() const { return !empty(); }
};

namespace glyph {

// What a control draws to report a state.
inline constexpr Glyph kCheck  {L"\uE73E"};  // CheckMark
inline constexpr Glyph kWarning{L"\uE7BA"};  // Warning
inline constexpr Glyph kError  {L"\uEA39"};  // ErrorBadge
inline constexpr Glyph kInfo   {L"\uE946"};  // Info
inline constexpr Glyph kBusy   {L"\uE895"};  // Sync: the ring a page spins while it waits


// The chrome: a pane's toggle, the arrows a combo and a scroll bar draw.
inline constexpr Glyph kMenu        {L"\uE700"};  // GlobalNavButton
inline constexpr Glyph kChevronDown {L"\uE70D"};  // ChevronDown
inline constexpr Glyph kChevronUp   {L"\uE70E"};  // ChevronUp
inline constexpr Glyph kChevronLeft {L"\uE76B"};  // ChevronLeft
inline constexpr Glyph kChevronRight{L"\uE76C"};  // ChevronRight
inline constexpr Glyph kCaretUp     {L"\uEDDB"};  // CaretUpSolid8
inline constexpr Glyph kCaretDown   {L"\uEDDC"};  // CaretDownSolid8
inline constexpr Glyph kRefresh     {L"\uE72C"};  // Refresh
inline constexpr Glyph kSettings    {L"\uE713"};  // Settings


// The verbs a menu, a toolbar, or a row of buttons is made of.
inline constexpr Glyph kAdd       {L"\uE710"};  // Add
inline constexpr Glyph kDelete    {L"\uE74D"};  // Delete
inline constexpr Glyph kEdit      {L"\uE70F"};  // Edit
inline constexpr Glyph kSave      {L"\uE74E"};  // Save
inline constexpr Glyph kCopy      {L"\uE8C8"};  // Copy
inline constexpr Glyph kCut       {L"\uE8C6"};  // Cut
inline constexpr Glyph kPaste     {L"\uE77F"};  // Paste
inline constexpr Glyph kUndo      {L"\uE7A7"};  // Undo
inline constexpr Glyph kRedo      {L"\uE7A6"};  // Redo
inline constexpr Glyph kSearch    {L"\uE721"};  // Search
inline constexpr Glyph kFilter    {L"\uE71C"};  // Filter
inline constexpr Glyph kSort      {L"\uE8CB"};  // Sort
inline constexpr Glyph kMore      {L"\uE712"};  // More
inline constexpr Glyph kClose     {L"\uE8BB"};  // ChromeClose
// The other X. ChromeClose is the caption button's, and it is drawn heavier than the icons a page
// puts beside it; this is the thin one, which is what a page's own button wants next to something
// like Refresh. Both are in Segoe MDL2 Assets at the same code point.
inline constexpr Glyph kCancel    {L"\uE711"};  // Cancel
// The cross a field is emptied with, and the one WinUI's own text box draws in its delete button
// (TextBox_themeresources.xaml). The heavier of the two crosses, which is what a button wants and
// what the thin `kCancel` beside a Refresh is not.
inline constexpr Glyph kClear     {L"\uE894"};  // Clear
// The switch a session is turned off with, beside a Refresh. Fluent draws it as the power button,
// which is what a button that ends a session is.
inline constexpr Glyph kPower     {L"\uE7E8"};  // PowerButton
inline constexpr Glyph kPin       {L"\uE718"};  // Pin
inline constexpr Glyph kLink      {L"\uE71B"};  // Link
inline constexpr Glyph kShare     {L"\uE72D"};  // Share
inline constexpr Glyph kDownload  {L"\uE896"};  // Download
inline constexpr Glyph kUpload    {L"\uE898"};  // Upload
inline constexpr Glyph kPrint     {L"\uE749"};  // Print
inline constexpr Glyph kNewFolder {L"\uE8F4"};  // NewFolder
inline constexpr Glyph kOpenFile  {L"\uE8E5"};  // OpenFile
inline constexpr Glyph kNewWindow {L"\uE8A7"};  // OpenInNewWindow


// Places a page lists, and the things in them.
inline constexpr Glyph kHome    {L"\uE80F"};  // Home
inline constexpr Glyph kRecent  {L"\uE823"};  // Recent
inline constexpr Glyph kHistory {L"\uE81C"};  // History
inline constexpr Glyph kFolder  {L"\uE8B7"};  // Folder
inline constexpr Glyph kDrive   {L"\uEDA2"};  // HardDrive
inline constexpr Glyph kDocument{L"\uE8A5"};  // Document
inline constexpr Glyph kCloud   {L"\uE753"};  // Cloud
inline constexpr Glyph kGlobe   {L"\uE774"};  // Globe
inline constexpr Glyph kLanguage{L"\uF2B7"};  // LocaleLanguage: a globe with a letter and a character on it
inline constexpr Glyph kEthernet    {L"\uE839"};  // Ethernet
inline constexpr Glyph kRemoteDevice{L"\uE836"};  // Add Remote Device
inline constexpr Glyph kCalendar{L"\uE787"};  // Calendar
inline constexpr Glyph kMail    {L"\uE715"};  // Mail
inline constexpr Glyph kContact {L"\uE77B"};  // Contact
inline constexpr Glyph kPeople  {L"\uE716"};  // People


// Devices, and what the user is trusted with.
inline constexpr Glyph kCamera    {L"\uE722"};  // Camera
inline constexpr Glyph kCellPhone {L"\uE8EA"};  // CellPhone: the device on the far end
inline constexpr Glyph kColor     {L"\uE790"};  // Color
inline constexpr Glyph kBrightness{L"\uE706"};  // Brightness
inline constexpr Glyph kLock      {L"\uE72E"};  // Lock
inline constexpr Glyph kUnlock    {L"\uE785"};  // Unlock
inline constexpr Glyph kShield    {L"\uEA18"};  // Shield
inline constexpr Glyph kBolt      {L"\uE945"};  // LightningBolt
inline constexpr Glyph kBug       {L"\uEBE8"};  // Bug


// Media, and the way a view is looked at.
inline constexpr Glyph kPlay      {L"\uE768"};  // Play
inline constexpr Glyph kPause     {L"\uE769"};  // Pause
inline constexpr Glyph kStop      {L"\uE71A"};  // Stop
inline constexpr Glyph kPrevious  {L"\uE892"};  // Previous
inline constexpr Glyph kNext      {L"\uE893"};  // Next
inline constexpr Glyph kVolume    {L"\uE767"};  // Volume
inline constexpr Glyph kMute      {L"\uE74F"};  // Mute
inline constexpr Glyph kView      {L"\uE890"};  // View
inline constexpr Glyph kFullScreen{L"\uE740"};  // FullScreen
inline constexpr Glyph kZoomIn    {L"\uE8A3"};  // ZoomIn
inline constexpr Glyph kZoomOut   {L"\uE71F"};  // ZoomOut


// ---------------------------------------------------------------- sets

// **A family of icons that mean one thing at different values**, in the order the values go.
//
// The code points are not the order and cannot be made into one. Microsoft's own table is the
// authority here, and it says the battery ramp is three ranges: `Battery10` is **E83F**, which
// sits *before* `Battery0` at E850, while `BatteryCharging10`, `BatterySaver9` and
// `BatterySaver10` sit far away at **EA93** to **EA95**. `AlignLeft`/`Center`/`Right` are
// contiguous and in the *opposite* order to their meanings. So a set is a table, and an index is
// a position in it -- never a base code point plus an offset.
//
// A page uses one two ways. To read one out, index the set: `battery::Levels[7]`. To have a slot
// that changes, hand the set to the icon and move the index: `card->icon = battery::Levels;` and
// later `card->icon.Face(7);` -- see `Icon`. The second keeps the slot cheap for every icon that
// is only ever one glyph.
struct Set {
    const Glyph *items = nullptr;
    int count = 0;
    constexpr int size() const { return count; }
    // Clamped rather than undefined, at both ends: a page computing an index from a value it did
    // not range is the ordinary case, and the nearest face is a better answer than nothing.
    constexpr Glyph at(int i) const {
        if (count <= 0) return Glyph();
        if (i < 0) i = 0;
        if (i >= count) i = count - 1;
        return items[i];
    }
    constexpr Glyph operator[](int i) const { return at(i); }
    template <typename E>
    constexpr Glyph operator[](E face) const { return at(static_cast<int>(face)); }
};


// The battery ramps. **Three families, and they are three drawings rather than one** -- measured,
// not guessed: `MobBattery0` lays down 2.5 times the ink `Battery0` does, so the `Mob` set is the
// same ramp at a heavier weight; `VerticalBattery0` matches `MobBattery0` to the pixel and is that
// weight turned a quarter turn. Which one a page wants is therefore a question about where the
// icon goes, and the names are Microsoft's so that it can be looked up rather than remembered.
//
// Each ramp runs 0 to 10, and the sets are ordered by that number. `BandBattery` is the Band's
// seven-step ramp.
namespace battery {
enum class Level : int { Empty, One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Full };
inline constexpr Glyph kLevels[] = {
    Glyph(L"\uE850"),   // Battery0
    Glyph(L"\uE851"),   // Battery1
    Glyph(L"\uE852"),   // Battery2
    Glyph(L"\uE853"),   // Battery3
    Glyph(L"\uE854"),   // Battery4
    Glyph(L"\uE855"),   // Battery5
    Glyph(L"\uE856"),   // Battery6
    Glyph(L"\uE857"),   // Battery7
    Glyph(L"\uE858"),   // Battery8
    Glyph(L"\uE859"),   // Battery9
    Glyph(L"\uE83F"),   // Battery10 -- E83F, before Battery0
};
inline constexpr Set Levels{kLevels, 11};
inline constexpr Glyph kCharging[] = {
    Glyph(L"\uE85A"),   // BatteryCharging0
    Glyph(L"\uE85B"),   // BatteryCharging1
    Glyph(L"\uE85C"),   // BatteryCharging2
    Glyph(L"\uE85D"),   // BatteryCharging3
    Glyph(L"\uE85E"),   // BatteryCharging4
    Glyph(L"\uE85F"),   // BatteryCharging5
    Glyph(L"\uE860"),   // BatteryCharging6
    Glyph(L"\uE861"),   // BatteryCharging7
    Glyph(L"\uE862"),   // BatteryCharging8
    Glyph(L"\uE83E"),   // BatteryCharging9  -- E83E
    Glyph(L"\uEA93"),   // BatteryCharging10 -- EA93
};
inline constexpr Set Charging{kCharging, 11};
inline constexpr Glyph kSaving[] = {
    Glyph(L"\uE863"),   // BatterySaver0
    Glyph(L"\uE864"),   // BatterySaver1
    Glyph(L"\uE865"),   // BatterySaver2
    Glyph(L"\uE866"),   // BatterySaver3
    Glyph(L"\uE867"),   // BatterySaver4
    Glyph(L"\uE868"),   // BatterySaver5
    Glyph(L"\uE869"),   // BatterySaver6
    Glyph(L"\uE86A"),   // BatterySaver7
    Glyph(L"\uE86B"),   // BatterySaver8
    Glyph(L"\uEA94"),   // BatterySaver9  -- EA94
    Glyph(L"\uEA95"),   // BatterySaver10 -- EA95
};
inline constexpr Set Saving{kSaving, 11};
inline constexpr Glyph kUnknown{L"\uE996"};           // BatteryUnknown
inline constexpr Glyph kUnknownCharging{L"\uEB1A"};   // BatteryUnknownCharging
// `BatterySaver` with no number: the badge, which is not one of `BatterySaver0..10`.
inline constexpr Glyph kSaverBadge{L"\uF432"};        // BatterySaver
// `SettingsBattery` -- the empty battery on a Settings page, and the one in this family whose
// name is nowhere near the others.
inline constexpr Glyph kSettingsBattery{L"\uEE63"};   // SettingsBattery
}  // namespace battery

namespace mobBattery {
enum class Level : int { Empty, One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Full };
inline constexpr Glyph kLevels[] = {
    Glyph(L"\uEBA0"),   // MobBattery0
    Glyph(L"\uEBA1"),   // MobBattery1
    Glyph(L"\uEBA2"),   // MobBattery2
    Glyph(L"\uEBA3"),   // MobBattery3
    Glyph(L"\uEBA4"),   // MobBattery4
    Glyph(L"\uEBA5"),   // MobBattery5
    Glyph(L"\uEBA6"),   // MobBattery6
    Glyph(L"\uEBA7"),   // MobBattery7
    Glyph(L"\uEBA8"),   // MobBattery8
    Glyph(L"\uEBA9"),   // MobBattery9
    Glyph(L"\uEBAA"),   // MobBattery10
};
inline constexpr Set Levels{kLevels, 11};
inline constexpr Glyph kCharging[] = {
    Glyph(L"\uEBAB"),   // MobBatteryCharging0
    Glyph(L"\uEBAC"),   // MobBatteryCharging1
    Glyph(L"\uEBAD"),   // MobBatteryCharging2
    Glyph(L"\uEBAE"),   // MobBatteryCharging3
    Glyph(L"\uEBAF"),   // MobBatteryCharging4
    Glyph(L"\uEBB0"),   // MobBatteryCharging5
    Glyph(L"\uEBB1"),   // MobBatteryCharging6
    Glyph(L"\uEBB2"),   // MobBatteryCharging7
    Glyph(L"\uEBB3"),   // MobBatteryCharging8
    Glyph(L"\uEBB4"),   // MobBatteryCharging9
    Glyph(L"\uEBB5"),   // MobBatteryCharging10
};
inline constexpr Set Charging{kCharging, 11};
inline constexpr Glyph kSaving[] = {
    Glyph(L"\uEBB6"),   // MobBatterySaver0
    Glyph(L"\uEBB7"),   // MobBatterySaver1
    Glyph(L"\uEBB8"),   // MobBatterySaver2
    Glyph(L"\uEBB9"),   // MobBatterySaver3
    Glyph(L"\uEBBA"),   // MobBatterySaver4
    Glyph(L"\uEBBB"),   // MobBatterySaver5
    Glyph(L"\uEBBC"),   // MobBatterySaver6
    Glyph(L"\uEBBD"),   // MobBatterySaver7
    Glyph(L"\uEBBE"),   // MobBatterySaver8
    Glyph(L"\uEBBF"),   // MobBatterySaver9
    Glyph(L"\uEBC0"),   // MobBatterySaver10
};
inline constexpr Set Saving{kSaving, 11};
inline constexpr Glyph kUnknown{L"\uEC02"};           // MobBatteryUnknown
inline constexpr Glyph kUnknownCharging{L"\uEB17"};   // MobBatteryUnknownCharging
}  // namespace mobBattery

namespace verticalBattery {
enum class Level : int { Empty, One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Full };
inline constexpr Glyph kLevels[] = {
    Glyph(L"\uF5F2"),   // VerticalBattery0
    Glyph(L"\uF5F3"),   // VerticalBattery1
    Glyph(L"\uF5F4"),   // VerticalBattery2
    Glyph(L"\uF5F5"),   // VerticalBattery3
    Glyph(L"\uF5F6"),   // VerticalBattery4
    Glyph(L"\uF5F7"),   // VerticalBattery5
    Glyph(L"\uF5F8"),   // VerticalBattery6
    Glyph(L"\uF5F9"),   // VerticalBattery7
    Glyph(L"\uF5FA"),   // VerticalBattery8
    Glyph(L"\uF5FB"),   // VerticalBattery9
    Glyph(L"\uF5FC"),   // VerticalBattery10
};
inline constexpr Set Levels{kLevels, 11};
// No saver ramp: the font has `VerticalBattery0..10` and `VerticalBatteryCharging0..10` and stops.
inline constexpr Glyph kCharging[] = {
    Glyph(L"\uF5FD"),   // VerticalBatteryCharging0
    Glyph(L"\uF5FE"),   // VerticalBatteryCharging1
    Glyph(L"\uF5FF"),   // VerticalBatteryCharging2
    Glyph(L"\uF600"),   // VerticalBatteryCharging3
    Glyph(L"\uF601"),   // VerticalBatteryCharging4
    Glyph(L"\uF602"),   // VerticalBatteryCharging5
    Glyph(L"\uF603"),   // VerticalBatteryCharging6
    Glyph(L"\uF604"),   // VerticalBatteryCharging7
    Glyph(L"\uF605"),   // VerticalBatteryCharging8
    Glyph(L"\uF606"),   // VerticalBatteryCharging9
    Glyph(L"\uF607"),   // VerticalBatteryCharging10
};
inline constexpr Set Charging{kCharging, 11};
inline constexpr Glyph kUnknown{L"\uF608"};   // VerticalBatteryUnknown
}  // namespace verticalBattery

namespace bandBattery {
enum class Level : int { Empty, One, Two, Three, Four, Five, Full };
inline constexpr Glyph kLevels[] = {
    Glyph(L"\uECB9"),   // BandBattery0
    Glyph(L"\uECBA"),   // BandBattery1
    Glyph(L"\uECBB"),   // BandBattery2
    Glyph(L"\uECBC"),   // BandBattery3
    Glyph(L"\uECBD"),   // BandBattery4
    Glyph(L"\uECBE"),   // BandBattery5
    Glyph(L"\uECBF"),   // BandBattery6
};
inline constexpr Set Levels{kLevels, 7};
}  // namespace bandBattery

// Where a run of text sits. The code points are `AlignRight`, `AlignCenter`, `AlignLeft` in that
// order -- the reverse of what an index of 0, 1, 2 should mean -- so the table is written in the
// order the settings go and the code points fall where they fall.
namespace align {
enum class Side : int { Left, Center, Right };
inline constexpr Glyph kSides[] = {
    Glyph(L"\uE8E4"),   // AlignLeft
    Glyph(L"\uE8E3"),   // AlignCenter
    Glyph(L"\uE8E2"),   // AlignRight
};
inline constexpr Set Sides{kSides, 3};
}  // namespace align

// How fast something is going: three faces, and `SpeedHigh2` beside them as a redrawing of the
// third rather than a fourth step.
namespace speed {
enum class Pace : int { Off, Medium, High };
inline constexpr Glyph kPaces[] = {
    Glyph(L"\uEC48"),   // SpeedOff
    Glyph(L"\uEC49"),   // SpeedMedium
    Glyph(L"\uEC4A"),   // SpeedHigh
};
inline constexpr Set Paces{kPaces, 3};
inline constexpr Glyph kHigh2{L"\uF42F"};   // SpeedHigh2
}  // namespace speed

// The frames the connection icons show while they are connecting. **They exist only in Segoe
// Fluent Icons**, so a machine whose font is Segoe MDL2 Assets draws a box instead. That is a
// question about the *running* font, not about what the program was compiled for -- the font is
// not shipped with the program -- so the answer is `HasGlyph()` at run time: see theme.h.
//
// The sets are the frames in the order they go. How long each one is on screen is not decided
// here -- nothing advances them yet, and `card->icon.Face(n)` is the only way to move one.
namespace animated {
inline constexpr Glyph kBars[] = {
    Glyph(L"\uEAA1"),   // SignalBarsAni1
    Glyph(L"\uEAA2"),   // SignalBarsAni2
    Glyph(L"\uEAA3"),   // SignalBarsAni3
};
inline constexpr Set Bars{kBars, 3};
inline constexpr Glyph kWifi[] = {
    Glyph(L"\uEAA5"),   // WifiAni1
    Glyph(L"\uEAA8"),   // WifiAni2
};
inline constexpr Set Wifi{kWifi, 2};
// `EthernetAni1` is one frame, so it is a glyph and not a set.
inline constexpr Glyph kEthernet{L"\uEAA4"};   // EthernetAni1
}  // namespace animated

}  // namespace glyph
}  // namespace micula
