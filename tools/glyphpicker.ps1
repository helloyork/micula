#Requires -Version 7.0
<#
.SYNOPSIS
    Browse an icon font by code point, and click a glyph to copy it.

.DESCRIPTION
    Draws a range of code points in a grid, dims the ones the font does not have, draws in the
    warning colour the ones that are missing from one of the fonts named by -Both, and puts the
    literal of the clicked cell on the clipboard. Clicking a cell again takes it back out of the list
    at the bottom, which a button copies in one go.

    What a picture *means* is the part no tool can answer: both Segoe icon fonts have an empty `post`
    table, so they carry no glyph names, and Microsoft's own icon lists (Windows Terminal's
    `SegoeFluentIconList.h`, the WinUI Gallery's `IconsData.json`) are the only place a name can be
    read from. This is for the other half of the job -- finding the code point once the name, or the
    shape, is known, and checking that both fonts have it, which every constant in `glyphs.h` must.

.PARAMETER Both
    Fonts a glyph has to exist in to be drawn in the normal colour. A glyph missing from one of them
    is drawn in the warning colour. Pass an empty list to stop testing the second font.

.PARAMETER Shot
    Open no window: render the first page to a PNG and exit. Handy for a quick look without clicking.

.PARAMETER SelfTest
    Open no window: copy something and read it back, then count how many code points in the range each
    font has. This puts a string on the clipboard, so do not run it while something else is being
    pasted.

.EXAMPLE
    pwsh -File tools/glyphpicker.ps1

.EXAMPLE
    pwsh -File tools/glyphpicker.ps1 -From 0xE700 -To 0xE7FF

.EXAMPLE
    pwsh -File tools/glyphpicker.ps1 -Shot icons.png -From 0xE836 -To 0xE839 -Cols 2
#>
[CmdletBinding()]
param(
    [int]      $From   = 0xE700,
    [int]      $To     = 0xF8FF,
    [string]   $Font   = 'Segoe Fluent Icons',
    [string[]] $Both   = @('Segoe Fluent Icons', 'Segoe MDL2 Assets'),
    [int]      $Cols   = 10,
    [int]      $Rows   = 6,
    [int]      $Cell   = 84,
    [string]   $Format = 'literal',
    [string]   $Shot,
    [switch]   $SelfTest
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing

if (-not ('GlyphTools' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class GlyphTools
{
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
    static extern uint GetGlyphIndicesW(IntPtr hdc, string s, int count, [Out] ushort[] gi, uint flags);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint flags, UIntPtr bytes);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr mem);
    [DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr mem);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalFree(IntPtr mem);
    [DllImport("user32.dll", SetLastError = true)] static extern bool OpenClipboard(IntPtr owner);
    [DllImport("user32.dll")] static extern bool EmptyClipboard();
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetClipboardData(uint format, IntPtr mem);
    [DllImport("user32.dll")] static extern IntPtr GetClipboardData(uint format);
    [DllImport("user32.dll")] static extern bool CloseClipboard();

    // GGI_MARK_NONEXISTING_GLYPHS: a character the font has no glyph for comes back as 0xFFFF.
    public static bool HasGlyph(IntPtr hdc, IntPtr hfont, char c)
    {
        IntPtr old = SelectObject(hdc, hfont);
        if (old == IntPtr.Zero || old == new IntPtr(-1)) return false;
        ushort[] gi = new ushort[1];
        uint n = GetGlyphIndicesW(hdc, new string(c, 1), 1, gi, 0x0001);
        SelectObject(hdc, old);
        return n != 0xFFFFFFFF && gi[0] != 0xFFFF;
    }

    // The plain Win32 clipboard rather than System.Windows.Forms.Clipboard: PowerShell 7 runs on an
    // MTA thread, and the WinForms one throws there. This one does not care about apartments.
    public static bool Copy(string text)
    {
        if (!OpenClipboard(IntPtr.Zero)) return false;
        try
        {
            EmptyClipboard();
            byte[] bytes = Encoding.Unicode.GetBytes(text + "\0");
            IntPtr mem = GlobalAlloc(0x0002, (UIntPtr)(uint)bytes.Length);   // GMEM_MOVEABLE
            if (mem == IntPtr.Zero) return false;
            IntPtr p = GlobalLock(mem);
            if (p == IntPtr.Zero) { GlobalFree(mem); return false; }
            Marshal.Copy(bytes, 0, p, bytes.Length);
            GlobalUnlock(mem);
            // The clipboard owns the block once this succeeds, so it must not be freed here.
            if (SetClipboardData(13, mem) == IntPtr.Zero) { GlobalFree(mem); return false; }
            return true;
        }
        finally { CloseClipboard(); }
    }

    public static string Paste()
    {
        if (!OpenClipboard(IntPtr.Zero)) return null;
        try
        {
            IntPtr h = GetClipboardData(13);
            if (h == IntPtr.Zero) return null;
            IntPtr p = GlobalLock(h);
            if (p == IntPtr.Zero) return null;
            try { return Marshal.PtrToStringUni(p); }
            finally { GlobalUnlock(h); }
        }
        finally { CloseClipboard(); }
    }
}
'@
}

# ---------------------------------------------------------------- state

$gap     = 3
$perPage = $Cols * $Rows

$script:page   = 0
$script:hover  = -1
$script:format = $Format
$script:picked = [System.Collections.Generic.HashSet[int]]::new()
$script:cache  = @{}
$script:hdc    = [IntPtr]::Zero
$script:hfonts = @{}

function Start-Measure {
    # One memory DC with each font selected into it is all GetGlyphIndices needs, and holding the
    # bitmap keeps that DC alive for the life of the window.
    $script:surface = [System.Drawing.Bitmap]::new(4, 4)
    $script:surfaceG = [System.Drawing.Graphics]::FromImage($script:surface)
    $script:hdc = $script:surfaceG.GetHdc()
    foreach ($name in (@($Font) + $Both | Select-Object -Unique)) {
        $f = [System.Drawing.Font]::new($name, 24.0, [System.Drawing.FontStyle]::Regular,
                                        [System.Drawing.GraphicsUnit]::Pixel)
        $script:hfonts[$name] = $f.ToHfont()
    }
}

function Test-Glyph([string]$name, [int]$cp) {
    if (-not $script:hfonts.ContainsKey($name)) { return $true }
    $key = "$name|$cp"
    if (-not $script:cache.ContainsKey($key)) {
        $script:cache[$key] = [GlyphTools]::HasGlyph($script:hdc, $script:hfonts[$name], [char]$cp)
    }
    $script:cache[$key]
}

function Test-Both([int]$cp) {
    foreach ($name in $Both) { if (-not (Test-Glyph $name $cp)) { return $false } }
    return $true
}

function Format-Cp([int]$cp) {
    switch ($script:format) {
        'character' { return [string][char]$cp }
        'hex'       { return '{0:X4}' -f $cp }
        default     { return 'L"\u{0:X4}"' -f $cp }
    }
}

function Start-Ink([double]$scale) {
    $script:iconFont = [System.Drawing.Font]::new($Font, [float]($Cell * $scale * 0.52),
                                                  [System.Drawing.FontStyle]::Regular,
                                                  [System.Drawing.GraphicsUnit]::Pixel)
    $script:codeFont = [System.Drawing.Font]::new('Consolas', [float](11 * $scale),
                                                  [System.Drawing.FontStyle]::Regular,
                                                  [System.Drawing.GraphicsUnit]::Pixel)
    $script:bg       = [System.Drawing.Color]::FromArgb(32, 32, 32)
    $script:ink      = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(238, 238, 238))
    $script:inkMissing = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(85, 85, 85))
    $script:inkPartial = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(228, 160, 60))
    $script:inkCode  = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(150, 150, 150))
    $script:hlFill   = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70, 0, 120, 215))
    $script:edge     = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 120, 215), [float](2 * $scale))
    $script:hoverPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(110, 255, 255, 255), 1)
    $script:centre   = [System.Drawing.StringFormat]::new()
    $script:centre.Alignment = [System.Drawing.StringAlignment]::Center
    $script:centre.LineAlignment = [System.Drawing.StringAlignment]::Center
    $script:centre.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap
}

function Draw-Page([System.Drawing.Graphics]$g, [int]$page) {
    $g.Clear($script:bg)
    for ($i = 0; $i -lt $perPage; $i++) {
        $cp = $From + $page * $perPage + $i
        if ($cp -gt $To) { break }
        $x = ($i % $Cols) * ($Cell + $gap)
        $y = [int][Math]::Floor($i / $Cols) * ($Cell + $gap)
        $box = [System.Drawing.Rectangle]::new($x, $y, $Cell, $Cell)
        if ($script:picked.Contains($cp)) { $g.FillRectangle($script:hlFill, $box) }
        if ($cp -eq $script:hover) { $g.DrawRectangle($script:hoverPen, $box) }
        $ink = if (-not (Test-Glyph $Font $cp)) { $script:inkMissing }
               elseif (-not (Test-Both $cp)) { $script:inkPartial }
               else { $script:ink }
        $g.DrawString([string][char]$cp, $script:iconFont, $ink,
                      [System.Drawing.RectangleF]::new($x, $y, $Cell, [float]($Cell * 0.74)), $script:centre)
        $g.DrawString(('{0:X4}' -f $cp), $script:codeFont, $script:inkCode,
                      [System.Drawing.RectangleF]::new($x, [float]($y + $Cell * 0.72), $Cell, [float]($Cell * 0.28)),
                      $script:centre)
        if ($script:picked.Contains($cp)) { $g.DrawRectangle($script:edge, $box) }
    }
}

function Hit-Cp([int]$x, [int]$y) {
    $c = [int][Math]::Floor($x / ($Cell + $gap))
    $r = [int][Math]::Floor($y / ($Cell + $gap))
    if ($c -lt 0 -or $c -ge $Cols -or $r -lt 0 -or $r -ge $Rows) { return -1 }
    $cp = $From + $script:page * $perPage + $r * $Cols + $c
    if ($cp -gt $To) { return -1 }
    return $cp
}

# ---------------------------------------------------------------- off-screen sheet

if ($SelfTest) {
    Start-Measure
    $literal = Format-Cp 0xE839
    $copied  = [GlyphTools]::Copy($literal)
    $back    = [GlyphTools]::Paste()
    Write-Host "clipboard: copy=$copied readback='$back' match=$($back -eq $literal)"
    foreach ($name in (@($Font) + $Both | Select-Object -Unique)) {
        $have = 0
        for ($cp = $From; $cp -le $To; $cp++) { if (Test-Glyph $name $cp) { $have++ } }
        Write-Host ('{0,-22} {1} of {2} code points' -f $name, $have, ($To - $From + 1))
    }
    return
}

if ($Shot) {
    Start-Measure
    Start-Ink 1.0
    $rows  = [int][Math]::Min($Rows, [Math]::Ceiling(($To - $From + 1) / $Cols))
    $sheet = [System.Drawing.Bitmap]::new($Cols * ($Cell + $gap), $rows * ($Cell + $gap))
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    Draw-Page $g 0
    $sheet.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $sheet.Dispose()
    Write-Host "wrote $Shot -- $Cols x $rows cells from U+$('{0:X4}' -f $From)"
    return
}

# ---------------------------------------------------------------- window

$form = [System.Windows.Forms.Form]::new()
$form.Text = "Glyph picker -- $Font"
$form.AutoScaleMode = [System.Windows.Forms.AutoScaleMode]::None
$form.KeyPreview = $true
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedSingle
$form.MaximizeBox = $false
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen

try { [System.Windows.Forms.Application]::SetHighDpiMode([System.Windows.Forms.HighDpiMode]::PerMonitorV2) } catch { }
$measure = $form.CreateGraphics()
$s = $measure.DpiX / 96.0
$measure.Dispose()

$Cell  = [int][Math]::Round($Cell * $s)
$gap   = [int](3 * $s)
$pad   = [int](10 * $s)
$line  = [int](28 * $s)

# -Cell is a maximum: a sheet taller or wider than the working area could not be browsed at all, so
# the cells give way instead.
$mins = [int](20 * $s)
$work = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$fitW = [int][Math]::Floor(($work.Width - 2 * $pad) / $Cols) - $gap
$fitH = [int][Math]::Floor(($work.Height - 6 * $line - 3 * $pad) / $Rows) - $gap
$Cell = [Math]::Max([Math]::Min($Cell, [Math]::Min($fitW, $fitH)), $mins)

$gridW = $Cols * ($Cell + $gap)
$gridH = $Rows * ($Cell + $gap)
$form.ClientSize = [System.Drawing.Size]::new($gridW + 2 * $pad, $gridH + 6 * $line + $pad * 3)

Start-Measure
Start-Ink 1.0

function Sx([int]$v) { [int]($v * $s) }   # a layout number, scaled for this monitor

$panel = [System.Windows.Forms.Panel]::new()
$panel.Location = [System.Drawing.Point]::new($pad, $line + $pad)
$panel.Size = [System.Drawing.Size]::new($gridW, $gridH)
# A hand-painted panel flickers unless it is double buffered, and that property is protected.
[System.Windows.Forms.Control].GetProperty('DoubleBuffered',
    [System.Reflection.BindingFlags]'Instance,NonPublic').SetValue($panel, $true)

$tbFrom = [System.Windows.Forms.TextBox]::new()
$tbFrom.Text = '{0:X4}' -f $From
$tbFrom.Location = [System.Drawing.Point]::new($pad + (Sx 40), $pad)
$tbFrom.Size = [System.Drawing.Size]::new((Sx 64), $line - (Sx 6))

$lblFrom = [System.Windows.Forms.Label]::new()
$lblFrom.Text = '范围'
$lblFrom.AutoSize = $true
$lblFrom.Location = [System.Drawing.Point]::new($pad, $pad + (Sx 5))

$lblDash = [System.Windows.Forms.Label]::new()
$lblDash.Text = '–'
$lblDash.AutoSize = $true
$lblDash.Location = [System.Drawing.Point]::new($pad + (Sx 108), $pad + (Sx 5))

$tbTo = [System.Windows.Forms.TextBox]::new()
$tbTo.Text = '{0:X4}' -f $To
$tbTo.Location = [System.Drawing.Point]::new($pad + (Sx 122), $pad)
$tbTo.Size = [System.Drawing.Size]::new((Sx 64), $line - (Sx 6))

$btnApply = [System.Windows.Forms.Button]::new()
$btnApply.Text = '应用'
$btnApply.Location = [System.Drawing.Point]::new($pad + (Sx 194), $pad - (Sx 1))
$btnApply.Size = [System.Drawing.Size]::new((Sx 64), $line)

$cbFmt = [System.Windows.Forms.ComboBox]::new()
$cbFmt.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
[void]$cbFmt.Items.AddRange(@('L"\uXXXX"', 'E839', '字符'))
$cbFmt.SelectedIndex = @('literal', 'hex', 'character').IndexOf($Format.ToLower())
$cbFmt.Location = [System.Drawing.Point]::new($pad + (Sx 330), $pad)
$cbFmt.Size = [System.Drawing.Size]::new((Sx 110), $line)

$lblFmt = [System.Windows.Forms.Label]::new()
$lblFmt.Text = '复制为'
$lblFmt.AutoSize = $true
$lblFmt.Location = [System.Drawing.Point]::new($pad + (Sx 274), $pad + (Sx 5))

$legend = [System.Windows.Forms.Label]::new()
$legend.Text = '灰 = 字体没有    橙 = 只有其中一种字体有'
$legend.AutoSize = $true

foreach ($ctl in @($lblFrom, $lblDash, $lblFmt, $legend)) { $form.Controls.Add($ctl) }
$legend.Location = [System.Drawing.Point]::new($form.ClientSize.Width - $legend.PreferredSize.Width - $pad, $pad + (Sx 5))

$lblPage = [System.Windows.Forms.Label]::new()
$lblPage.AutoSize = $true

$btnPrev = [System.Windows.Forms.Button]::new()
$btnPrev.Text = '‹'
$btnPrev.Size = [System.Drawing.Size]::new($line, $line)

$btnNext = [System.Windows.Forms.Button]::new()
$btnNext.Text = '›'
$btnNext.Size = [System.Drawing.Size]::new($line, $line)

$btnCopy = [System.Windows.Forms.Button]::new()
$btnCopy.Size = [System.Drawing.Size]::new((Sx 120), $line)

$btnClear = [System.Windows.Forms.Button]::new()
$btnClear.Text = '清空'
$btnClear.Size = [System.Drawing.Size]::new((Sx 64), $line)

$status = [System.Windows.Forms.Label]::new()
$status.AutoSize = $true

$tbPicks = [System.Windows.Forms.TextBox]::new()
$tbPicks.ReadOnly = $true
$tbPicks.Multiline = $true
$tbPicks.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
$tbPicks.Font = [System.Drawing.Font]::new('Consolas', [float]($script:codeFont.Size), [System.Drawing.FontStyle]::Regular,
                                            [System.Drawing.GraphicsUnit]::Pixel)

$navY  = $line * 2 + $pad + $gridH
$pickY = $navY + $line + (Sx 4)

foreach ($ctl in @($tbFrom, $tbTo, $btnApply, $cbFmt)) { $form.Controls.Add($ctl) }
foreach ($ctl in @($btnPrev, $lblPage, $btnNext, $btnCopy, $btnClear, $status)) { $form.Controls.Add($ctl) }
$form.Controls.Add($panel)
$form.Controls.Add($tbPicks)

$btnPrev.Location   = [System.Drawing.Point]::new($pad, $navY)
$lblPage.Location   = [System.Drawing.Point]::new($pad + $line + (Sx 8), $navY + (Sx 5))
$btnNext.Location   = [System.Drawing.Point]::new($pad + $line + (Sx 190), $navY)
$btnCopy.Location   = [System.Drawing.Point]::new($pad + $line + (Sx 228), $navY)
$btnClear.Location  = [System.Drawing.Point]::new($pad + $line + (Sx 356), $navY)
$status.Location    = [System.Drawing.Point]::new($pad + $line + (Sx 428), $navY + (Sx 5))
$tbPicks.Location   = [System.Drawing.Point]::new($pad, $pickY)
$tbPicks.Size       = [System.Drawing.Size]::new($gridW, $line * 2 + (Sx 10))

$script:panel   = $panel
$script:status  = $status
$script:tbPicks = $tbPicks
$script:btnCopy = $btnCopy
$script:lblPage = $lblPage
$script:tbFrom  = $tbFrom
$script:tbTo    = $tbTo
$script:cbFmt   = $cbFmt

$script:pages = [Math]::Max(1, [int][Math]::Ceiling(($To - $From + 1) / $perPage))

function Update-Picks {
    $items = @($script:picked | Sort-Object)
    $script:tbPicks.Text = ($items | ForEach-Object { Format-Cp $_ }) -join "`r`n"
    $script:btnCopy.Text = "复制已选 ($($items.Count))"
}

function Update-Status {
    if ($script:hover -lt 0) {
        $script:status.Text = '点一下格子就复制；再点一次取消'
        return
    }
    $cp = $script:hover
    $have = if (Test-Glyph $Font $cp) { '有' } else { '这个字体没有' }
    $both = if (Test-Both $cp) { '两字体都有' } else { '另一字体没有' }
    $script:status.Text = 'U+{0:X4}  {1}  ·  {2}' -f $cp, $have, $both
}

function Show-Page([int]$p) {
    $script:page = [Math]::Max(0, [Math]::Min($p, $script:pages - 1))
    $lo = $From + $script:page * $perPage
    $hi = [Math]::Min($To, $lo + $perPage - 1)
    $script:lblPage.Text = '第 {0}/{1} 页   U+{2:X4} – U+{3:X4}' -f ($script:page + 1), $script:pages, $lo, $hi
    $script:panel.Invalidate()
    Update-Status
}

function Pick-Cp([int]$cp) {
    $literal = Format-Cp $cp
    $ok = [GlyphTools]::Copy($literal)
    if ($script:picked.Contains($cp)) { [void]$script:picked.Remove($cp) } else { [void]$script:picked.Add($cp) }
    $script:panel.Invalidate()
    Update-Picks
    $script:status.Text = if ($ok) { "已复制 $literal" } else { '复制失败：剪贴板正被别的程序占着' }
}

function Read-Cp([string]$text) {
    $text = $text.Trim()
    if ($text -match '^(0x)?([0-9A-Fa-f]{2,6})$') { return [Convert]::ToInt32($matches[2], 16) }
    return -1
}

function Apply-Range {
    $f = Read-Cp $script:tbFrom.Text
    $t = Read-Cp $script:tbTo.Text
    if ($f -lt 0 -or $t -lt 0 -or $t -lt $f) {
        $script:status.Text = '范围写错了：给十六进制数，比如 E700'
        return
    }
    $script:From = $f
    $script:To   = $t
    $script:pages = [Math]::Max(1, [int][Math]::Ceiling(($t - $f + 1) / $perPage))
    Show-Page 0
}

$panel.Add_Paint({ param($s, $e) Draw-Page $e.Graphics $script:page })

$panel.Add_MouseMove({
    param($s, $e)
    $cp = Hit-Cp $e.X $e.Y
    if ($cp -ne $script:hover) { $script:hover = $cp; $s.Invalidate(); Update-Status }
})

$panel.Add_MouseLeave({ param($s, $e) $script:hover = -1; $s.Invalidate(); Update-Status })

$panel.Add_MouseClick({
    param($s, $e)
    if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left) {
        $cp = Hit-Cp $e.X $e.Y
        if ($cp -ge 0) { Pick-Cp $cp }
    }
})

$btnPrev.Add_Click({ Show-Page ($script:page - 1) })
$btnNext.Add_Click({ Show-Page ($script:page + 1) })

$btnCopy.Add_Click({
    $items = @($script:picked | Sort-Object)
    if ($items.Count -eq 0) { $script:status.Text = '还没选'; return }
    $text = ($items | ForEach-Object { Format-Cp $_ }) -join "`r`n"
    if ([GlyphTools]::Copy($text)) { $script:status.Text = "已复制 $($items.Count) 行" }
})

$btnClear.Add_Click({
    $script:picked.Clear()
    Update-Picks
    $script:panel.Invalidate()
    $script:status.Text = '清单清空了'
})

$btnApply.Add_Click({ Apply-Range })

$tbFrom.Add_KeyDown({ param($s, $e) if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Return) { Apply-Range; $e.SuppressKeyPress = $true } })
$tbTo.Add_KeyDown({ param($s, $e) if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Return) { Apply-Range; $e.SuppressKeyPress = $true } })

$cbFmt.Add_SelectedIndexChanged({
    $script:format = @('literal', 'hex', 'character')[$script:cbFmt.SelectedIndex]
    if ($script:tbPicks) { Update-Picks; Update-Status }
})

$form.Add_KeyDown({
    param($s, $e)
    if ($e.KeyCode -eq [System.Windows.Forms.Keys]::PageDown) { Show-Page ($script:page + 1); $e.Handled = $true }
    elseif ($e.KeyCode -eq [System.Windows.Forms.Keys]::PageUp) { Show-Page ($script:page - 1); $e.Handled = $true }
})

Update-Picks
Show-Page 0
[void]$form.ShowDialog()

$items = @($script:picked | Sort-Object)
if ($items.Count -gt 0) { Write-Host ($items | ForEach-Object { Format-Cp $_ }) }
