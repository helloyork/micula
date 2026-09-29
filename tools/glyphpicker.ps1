#Requires -Version 7.0
<#
.SYNOPSIS
    Browse an icon font by code point, and click a glyph to copy it.

.DESCRIPTION
    Draws a page of code points, dims the ones the font does not have, draws in the warning colour the
    ones that are missing from one of the fonts named by -Both, and puts the literal of the clicked
    cell on the clipboard. Clicking a cell again takes it back out of the list at the bottom, which a
    button copies in one go.

    The arrows beside the range flip the page: one page, or ten at a time. PageUp and PageDown do the
    same from the keyboard. The range is the sheet's bounds, so it is typed once and not per page.

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
    pwsh -File tools/glyphpicker.ps1 -Shot icons.png -From 0xE836 -To 0xE839 -Cols 2 -Cell 96
#>
[CmdletBinding()]
param(
    [int]      $From   = 0xE700,
    [int]      $To     = 0xF8FF,
    [string]   $Font   = 'Segoe Fluent Icons',
    [string[]] $Both   = @('Segoe Fluent Icons', 'Segoe MDL2 Assets'),
    [int]      $Cols   = 14,
    [int]      $Rows   = 6,
    [int]      $Cell   = 48,
    [string]   $Format = 'literal',
    [string]   $Shot,
    [switch]   $SelfTest
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing

# PowerShell starts DPI unaware, and the shell then stretches the whole window to the real DPI, which
# is blurry text and coordinates that do not match the numbers in this script. This has to happen
# before the first control exists; after that WinForms refuses to change it, and the process stays
# unaware -- which is what the first version of this tool did, at 150%, to look like a GDI scaling bug.
try { [void][System.Windows.Forms.Application]::SetHighDpiMode([System.Windows.Forms.HighDpiMode]::PerMonitorV2) }
catch { Write-Warning "could not become DPI aware: $($_.Exception.Message)" }

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
    $script:surface  = [System.Drawing.Bitmap]::new(4, 4)
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

function Draw-Page([System.Drawing.Graphics]$g, [int]$page) {
    $g.Clear($script:bg)
    for ($i = 0; $i -lt $perPage; $i++) {
        $cp = $From + $page * $perPage + $i
        if ($cp -gt $To) { break }
        $x = ($i % $Cols) * $script:pitch
        $y = [int][Math]::Floor($i / $Cols) * $script:pitch
        $box = [System.Drawing.Rectangle]::new($x, $y, $script:Cell, $script:Cell)
        if ($script:picked.Contains($cp)) { $g.FillRectangle($script:hlFill, $box) }
        if ($cp -eq $script:hover) { $g.DrawRectangle($script:hoverPen, $box) }
        $ink = if (-not (Test-Glyph $Font $cp)) { $script:inkMissing }
               elseif (-not (Test-Both $cp)) { $script:inkPartial }
               else { $script:ink }
        $g.DrawString([string][char]$cp, $script:iconFont, $ink,
                      [System.Drawing.RectangleF]::new($x, $y, $script:Cell, $script:Cell * 0.7), $script:centre)
        $g.DrawString(('{0:X4}' -f $cp), $script:codeFont, $script:inkCode,
                      [System.Drawing.RectangleF]::new($x, $y + $script:Cell * 0.7, $script:Cell,
                                                       $script:Cell * 0.3), $script:centre)
        if ($script:picked.Contains($cp)) { $g.DrawRectangle($script:edge, $box) }
    }
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
    $script:Cell   = $Cell
    $script:pitch  = $Cell + 3
    $script:bg     = [System.Drawing.Color]::FromArgb(32, 32, 32)
    $script:ink        = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(238, 238, 238))
    $script:inkMissing = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(85, 85, 85))
    $script:inkPartial = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(228, 160, 60))
    $script:inkCode    = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(150, 150, 150))
    $script:hlFill     = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70, 0, 120, 215))
    $script:edge       = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 120, 215), 2)
    $script:hoverPen   = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(110, 255, 255, 255), 1)
    $script:iconFont   = [System.Drawing.Font]::new($Font, $Cell * 0.54, [System.Drawing.FontStyle]::Regular,
                                                    [System.Drawing.GraphicsUnit]::Pixel)
    $script:codeFont   = [System.Drawing.Font]::new('Consolas', [Math]::Max(7.0, $Cell * 0.18),
                                                    [System.Drawing.FontStyle]::Regular,
                                                    [System.Drawing.GraphicsUnit]::Pixel)
    $script:centre     = [System.Drawing.StringFormat]::new()
    $script:centre.Alignment = [System.Drawing.StringAlignment]::Center
    $script:centre.LineAlignment = [System.Drawing.StringAlignment]::Center
    $script:centre.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap

    $rows  = [int][Math]::Min($Rows, [Math]::Ceiling(($To - $From + 1) / $Cols))
    $sheet = [System.Drawing.Bitmap]::new($Cols * $script:pitch, $rows * $script:pitch)
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

$measure = $form.CreateGraphics()
$s = $measure.DpiX / 96.0
$measure.Dispose()

function Sx([double]$v) { [int][Math]::Round($v * $s) }   # a layout number, for the monitor's DPI

# Everything above is in 96 dpi units: -Cell is the cell size, and it is a maximum, so a page that
# would not fit the working area gets smaller cells rather than a window that hangs off the screen.
$gap   = Sx 2
$pad   = Sx 8
$line  = Sx 26
$script:Cell  = [int][Math]::Min((Sx $Cell), [Math]::Min(
                    [Math]::Floor(([System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Width - 4 * $pad) / $Cols) - $gap,
                    [Math]::Floor(([System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Height - 6 * $line - 6 * $pad) / $Rows) - $gap))
$script:Cell  = [Math]::Max($script:Cell, (Sx 20))
$script:pitch = $script:Cell + $gap

$gridW = $Cols * $script:pitch
$gridH = $Rows * $script:pitch
$wide  = [Math]::Max($gridW, (Sx 700))        # the bottom row of buttons is wider than a small grid
$top   = $pad + $line + $pad

$form.ClientSize = [System.Drawing.Size]::new($wide + 2 * $pad, $top + $gridH + $pad + $line + 4 + 2 * $line + $pad)

Start-Measure

# ---------------------------------------------------------------- the window's own drawing

$script:bg         = [System.Drawing.Color]::FromArgb(32, 32, 32)
$script:ink        = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(238, 238, 238))
$script:inkMissing = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(85, 85, 85))
$script:inkPartial = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(228, 160, 60))
$script:inkCode    = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(150, 150, 150))
$script:hlFill     = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70, 0, 120, 215))
$script:edge       = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 120, 215), [float](Sx 2))
$script:hoverPen   = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(110, 255, 255, 255), 1)
$script:iconFont   = [System.Drawing.Font]::new($Font, [float]($script:Cell * 0.54),
                                                [System.Drawing.FontStyle]::Regular,
                                                [System.Drawing.GraphicsUnit]::Pixel)
$script:codeFont   = [System.Drawing.Font]::new('Consolas', [float][Math]::Max((Sx 8), $script:Cell * 0.18),
                                                [System.Drawing.FontStyle]::Regular,
                                                [System.Drawing.GraphicsUnit]::Pixel)
$script:centre     = [System.Drawing.StringFormat]::new()
$script:centre.Alignment = [System.Drawing.StringAlignment]::Center
$script:centre.LineAlignment = [System.Drawing.StringAlignment]::Center
$script:centre.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap

$panel = [System.Windows.Forms.Panel]::new()
$panel.Location = [System.Drawing.Point]::new($pad, $top)
$panel.Size = [System.Drawing.Size]::new($gridW, $gridH)
# A hand-painted panel flickers unless it is double buffered, and that property is protected.
[System.Windows.Forms.Control].GetProperty('DoubleBuffered',
    [System.Reflection.BindingFlags]'Instance,NonPublic').SetValue($panel, $true)

function New-Label([string]$text, [int]$x, [int]$y) {
    $l = [System.Windows.Forms.Label]::new()
    $l.Text = $text
    $l.AutoSize = $true
    $l.Location = [System.Drawing.Point]::new($x, $y)
    $form.Controls.Add($l)
    return $l
}

function New-Button([string]$text, [int]$x, [int]$y, [int]$w) {
    $b = [System.Windows.Forms.Button]::new()
    $b.Text = $text
    $b.Location = [System.Drawing.Point]::new($x, $y)
    $b.Size = [System.Drawing.Size]::new($w, $line)
    $form.Controls.Add($b)
    return $b
}

$ty = $pad + (Sx 4)

[void](New-Label '范围' $pad $ty)
[void](New-Label '–' (Sx 120) $ty)
[void](New-Label '复制为' (Sx 8) ($top + $gridH + $pad + (Sx 6)))

$tbFrom = [System.Windows.Forms.TextBox]::new()
$tbFrom.Text = '{0:X4}' -f $From
$tbFrom.Location = [System.Drawing.Point]::new((Sx 46), $pad)
$tbFrom.Size = [System.Drawing.Size]::new((Sx 70), $line - (Sx 6))
$form.Controls.Add($tbFrom)

$tbTo = [System.Windows.Forms.TextBox]::new()
$tbTo.Text = '{0:X4}' -f $To
$tbTo.Location = [System.Drawing.Point]::new((Sx 136), $pad)
$tbTo.Size = [System.Drawing.Size]::new((Sx 70), $line - (Sx 6))
$form.Controls.Add($tbTo)

$btnApply = New-Button '应用' (Sx 214) $pad (Sx 62)

# The arrows are the point of the range being a range: the code is typed here, the pages are flipped
# there, and nothing has to be typed again to move.
$btnFirst = New-Button '«' (Sx 286) $pad (Sx 30)
$btnPrev  = New-Button '‹' (Sx 320) $pad (Sx 30)
$btnNext  = New-Button '›' (Sx 354) $pad (Sx 30)
$btnLast  = New-Button '»' (Sx 388) $pad (Sx 30)

$lblPage = New-Label '第 1/1 页' (Sx 430) $ty
$lblWhere = New-Label '' (Sx 600) $ty

$cbFmt = [System.Windows.Forms.ComboBox]::new()
$cbFmt.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
[void]$cbFmt.Items.AddRange(@('L"\uXXXX"', 'E839', '字符'))
$cbFmt.SelectedIndex = @('literal', 'hex', 'character').IndexOf($Format.ToLower())
$cbFmt.Location = [System.Drawing.Point]::new((Sx 64), ($top + $gridH + $pad + (Sx 2)))
$cbFmt.Size = [System.Drawing.Size]::new((Sx 110), $line)
$form.Controls.Add($cbFmt)

$btnCopy  = New-Button '复制已选 (0)' (Sx 184) ($top + $gridH + $pad) (Sx 130)
$btnClear = New-Button '清空' (Sx 322) ($top + $gridH + $pad) (Sx 62)
$status   = New-Label '' (Sx 396) ($top + $gridH + $pad + (Sx 6))

$tbPicks = [System.Windows.Forms.TextBox]::new()
$tbPicks.ReadOnly = $true
$tbPicks.Multiline = $true
$tbPicks.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
$tbPicks.Font = [System.Drawing.Font]::new('Consolas', [float]($script:codeFont.Size),
                                           [System.Drawing.FontStyle]::Regular,
                                           [System.Drawing.GraphicsUnit]::Pixel)
$tbPicks.Location = [System.Drawing.Point]::new($pad, ($top + $gridH + $pad + $line + (Sx 4)))
$tbPicks.Size = [System.Drawing.Size]::new($wide, 2 * $line)
$form.Controls.Add($tbPicks)
$form.Controls.Add($panel)

$script:panel   = $panel
$script:status  = $status
$script:tbPicks = $tbPicks
$script:btnCopy = $btnCopy
$script:lblPage = $lblPage
$script:lblWhere = $lblWhere
$script:tbFrom  = $tbFrom
$script:tbTo    = $tbTo
$script:cbFmt   = $cbFmt

$lblPage.Location  = [System.Drawing.Point]::new((Sx 430), $ty)
$lblWhere.Location = [System.Drawing.Point]::new((Sx 600), $ty)

$script:pages = [Math]::Max(1, [int][Math]::Ceiling(($To - $From + 1) / $perPage))

# The hint is the legend: there is no room for one of its own, and this is where an eye goes anyway.
$hint = '灰 = 没字形   橙 = 只有一种字体有   点格子复制'

function Update-Picks {
    $items = @($script:picked | Sort-Object)
    $script:tbPicks.Text = ($items | ForEach-Object { Format-Cp $_ }) -join "`r`n"
    $script:btnCopy.Text = "复制已选 ($($items.Count))"
}

function Update-Status {
    if ($script:hover -lt 0) {
        $script:status.Text = $hint
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
    $script:lblPage.Text  = '第 {0}/{1} 页' -f ($script:page + 1), $script:pages
    $script:lblWhere.Text = 'U+{0:X4} – U+{1:X4}' -f $lo, $hi
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
    $script:From  = $f
    $script:To    = $t
    $script:pages = [Math]::Max(1, [int][Math]::Ceiling(($t - $f + 1) / $perPage))
    Show-Page 0
}

$panel.Add_Paint({ param($s, $e) Draw-Page $e.Graphics $script:page })

$panel.Add_MouseMove({
    param($s, $e)
    $cp = if ($e.X -lt 0 -or $e.Y -lt 0) { -1 } else {
        $i = [int][Math]::Floor($e.Y / $script:pitch) * $Cols + [int][Math]::Floor($e.X / $script:pitch)
        $at = $From + $script:page * $perPage + $i
        if ($i -ge $perPage -or $at -gt $To) { -1 } else { $at }
    }
    if ($cp -ne $script:hover) { $script:hover = $cp; $s.Invalidate(); Update-Status }
})

$panel.Add_MouseLeave({ param($s, $e) $script:hover = -1; $s.Invalidate(); Update-Status })

$panel.Add_MouseClick({
    param($s, $e)
    if ($e.Button -ne [System.Windows.Forms.MouseButtons]::Left) { return }
    $i = [int][Math]::Floor($e.Y / $script:pitch) * $Cols + [int][Math]::Floor($e.X / $script:pitch)
    $cp = $From + $script:page * $perPage + $i
    if ($e.X -lt 0 -or $e.Y -lt 0 -or $i -ge $perPage -or $cp -gt $To) { return }
    Pick-Cp $cp
})

$btnApply.Add_Click({ Apply-Range })
$btnPrev.Add_Click({ Show-Page ($script:page - 1) })
$btnNext.Add_Click({ Show-Page ($script:page + 1) })
$btnFirst.Add_Click({ Show-Page ($script:page - 10) })
$btnLast.Add_Click({ Show-Page ($script:page + 10) })

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

foreach ($box in @($tbFrom, $tbTo)) {
    $box.Add_KeyDown({ param($s, $e) if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Return) { Apply-Range; $e.SuppressKeyPress = $true } })
}

$cbFmt.Add_SelectedIndexChanged({
    $script:format = @('literal', 'hex', 'character')[$script:cbFmt.SelectedIndex]
    if ($script:tbPicks) { Update-Picks; Update-Status }
})

$form.Add_KeyDown({
    param($s, $e)
    if ($e.KeyCode -eq [System.Windows.Forms.Keys]::PageDown) {
        Show-Page ($script:page + $(if ($e.Shift) { 10 } else { 1 })); $e.Handled = $true
    }
    elseif ($e.KeyCode -eq [System.Windows.Forms.Keys]::PageUp) {
        Show-Page ($script:page - $(if ($e.Shift) { 10 } else { 1 })); $e.Handled = $true
    }
})

Update-Picks
Show-Page 0
[void]$form.ShowDialog()

$items = @($script:picked | Sort-Object)
if ($items.Count -gt 0) { Write-Host ($items | ForEach-Object { Format-Cp $_ }) }
