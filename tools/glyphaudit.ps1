# Audits include/micula/glyphs.h against Microsoft's own icon tables.
#
#     pwsh -File tools/glyphaudit.ps1
#     pwsh -File tools/glyphaudit.ps1 -Refresh      # fetch the two pages again
#     pwsh -File tools/glyphaudit.ps1 -WriteNames   # regenerate glyph-names.csv from them
#
# Three questions, and none can be answered by eye:
#
#   1. Does every name in the header match Microsoft's name for that code point? A typo in a
#      trailing comment is invisible until somebody looks the icon up and finds the wrong one.
#   2. Is every glyph of a family that the header groups actually in the header? The battery
#      ramp is drawn across three ranges and one of its members is called `SettingsBattery`,
#      which no interval search will find -- the only reliable way is to start from the names.
#   3. Is `glyph-names.csv` -- the copy the picker labels cells with -- still the copy of these
#      tables? A table that quietly changed leaves the picker naming the wrong icon.
#
# Microsoft's two tables are the authority for *names*. They are not an authority for what a
# font draws: the pages list glyphs that a shipped font may not have, so presence is still a
# question for `tools/glyphpicker.ps1`.
param(
    [string] $Header = (Join-Path $PSScriptRoot '..\include\micula\glyphs.h'),
    [string] $Cache = (Join-Path $env:TEMP 'micula-glyphaudit'),
    [switch] $Refresh,
    [switch] $WriteNames
)

$ErrorActionPreference = 'Stop'
$pages = @{
    fluent = 'https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font'
    mdl2   = 'https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font'
}

if (-not (Test-Path $Cache)) { New-Item -ItemType Directory -Path $Cache | Out-Null }

function Table([string] $key) {
    $path = Join-Path $Cache "$key.html"
    if ($Refresh -or -not (Test-Path $path)) {
        Write-Host "fetching $($pages[$key])"
        Invoke-WebRequest -Uri $pages[$key] -UseBasicParsing -OutFile $path
    }
    # Each row renders as: <td><img .../e850.png" alt="Screenshot of Battery0."></td>
    #                      <td>e850</td><td><span class="no-loc" ...>Battery0</span></td>
    # The code point cell is the anchor; the digits go up to F8CC, so the class must accept F.
    $html = [System.IO.File]::ReadAllText($path)
    $map = @{}
    foreach ($m in [regex]::Matches($html, '(?s)<td>([0-9a-fA-F]{3,4})</td>\s*<td>(?:<span[^>]*>)?([^<]+)(?:</span>)?</td>')) {
        $cp = [Convert]::ToInt32($m.Groups[1].Value, 16)
        if (-not $map.ContainsKey($cp)) { $map[$cp] = $m.Groups[2].Value.Trim() }
    }
    $map
}

$fluent = Table 'fluent'
$mdl2   = Table 'mdl2'
if ($fluent.Count -eq 0) { throw 'the Fluent table did not parse -- the page markup has changed' }
if ($mdl2.Count -eq 0) { throw 'the MDL2 table did not parse -- the page markup has changed' }

# ---------------------------------------------------------------- the name table the tools read
# Neither font carries glyph names -- both have an empty `post` table -- so this file is the only
# place a name can come from, and `glyphpicker.ps1` reads it to label a cell. It is generated here
# so that it cannot be the product of a script nobody has any more, and checked here so that a
# table Microsoft changed cannot leave it naming the wrong icon.
$nameFile = Join-Path $PSScriptRoot 'glyph-names.csv'
$expected = @(($fluent.Keys + $mdl2.Keys) | Sort-Object -Unique |
              ForEach-Object { '{0:X4},{1},{2}' -f $_, $fluent[$_], $mdl2[$_] })

if ($WriteNames) {
    # NoBOM and the platform's own line endings: a BOM on the first line would turn the `code`
    # column of row one into `?code` for anything that reads the file as UTF-8.
    [System.IO.File]::WriteAllLines($nameFile, (@('code,fluent,mdl2') + $expected),
                                    [System.Text.UTF8Encoding]::new($false))
    Write-Host "wrote $nameFile -- $($expected.Count) code points"
}

$stale = @()
if (-not $WriteNames) {
    if (-not (Test-Path $nameFile)) {
        $stale = @('glyph-names.csv is not beside this script -- run it with -WriteNames')
    } else {
        $lines = @([System.IO.File]::ReadAllLines($nameFile))
        if ($lines.Count -eq 0 -or $lines[0] -ne 'code,fluent,mdl2') {
            $stale = @('glyph-names.csv does not start with its header row -- run this with -WriteNames')
        } else {
            $theirs = [System.Collections.Generic.HashSet[string]]::new([string[]]$expected)
            foreach ($line in @($lines | Select-Object -Skip 1)) {
                if (-not $theirs.Remove($line)) { $stale += "  - $line  (Microsoft no longer has this row)" }
            }
            foreach ($line in @($theirs | Sort-Object)) { $stale += "  + $line  (missing from the copy)" }
        }
    }
}

# ---------------------------------------------------------------- what the header says
$text = [System.IO.File]::ReadAllText((Resolve-Path $Header))
$stated = @()
# `inline constexpr Glyph kName{L"\uE850"};  // Battery0`  and  `Glyph(L"\uE850"),  // Battery0`
foreach ($m in [regex]::Matches($text, 'Glyph(?:\s+k\w+)?\s*[({]L"\\u([0-9A-Fa-f]{4})"[)}][,;]?[ \t]*//[ \t]*([^\r\n]+)')) {
    $cp = [Convert]::ToInt32($m.Groups[1].Value, 16)
    # A comment may explain itself after `--`, or name the icon before a `:`; the name is the head.
    $name = (($m.Groups[2].Value -split '\s+--\s+')[0] -split ':')[0].Trim()
    $stated += [pscustomobject]@{ code = $cp; name = $name }
}

Write-Host ''
Write-Host "header: $($stated.Count) named code points, $($fluent.Count) in Microsoft's Fluent table"
Write-Host ''

$wrong = @()
foreach ($s in $stated) {
    # Spaces are the header's to spend -- `Add Remote Device` is Microsoft's `AddRemoteDevice`.
    $mine = ($s.name -replace '\s', '')
    if (-not $fluent.ContainsKey($s.code)) {
        $wrong += ('{0:X4}  "{1}"  -- Microsoft has no name at this code point' -f $s.code, $s.name)
    } elseif (($fluent[$s.code] -replace '\s', '') -ne $mine) {
        $wrong += ('{0:X4}  header says "{1}", Microsoft says "{2}"' -f $s.code, $s.name, $fluent[$s.code])
    }
}
if ($wrong.Count) {
    Write-Host "WRONG NAMES ($($wrong.Count)):"
    $wrong | ForEach-Object { '  ' + $_ }
} else {
    Write-Host 'every name in the header matches Microsoft''s'
}

# ---------------------------------------------------------------- families that must be whole
# A family is a run of names that share a prefix; the prefix is the whole of what Microsoft
# promises, so starting from the names is the only search that cannot miss a stray.
$families = 'Battery', 'MobBattery', 'VerticalBattery', 'BandBattery', 'SettingsBattery',
            'Align', 'Speed'
$have = @{}
foreach ($s in $stated) { $have[$s.name] = $true }

$gaps = @()
foreach ($family in $families) {
    $names = @($fluent.Keys | Where-Object { $fluent[$_] -like "$family*" } | ForEach-Object { $fluent[$_] })
    $missing = @($names | Where-Object { -not $have.ContainsKey($_) })
    if ($missing.Count) {
        $gaps += ('{0}: {1} of {2} missing -- {3}' -f $family, $missing.Count, $names.Count, ($missing -join ', '))
    }
}
if ($gaps.Count) {
    Write-Host ''
    Write-Host 'FAMILIES THAT ARE NOT WHOLE:'
    $gaps | ForEach-Object { '  ' + $_ }
} else {
    Write-Host 'every glyph of every grouped family is in the header'
}

# ---------------------------------------------------------------- what Windows 10 cannot draw
# Not a failure, and deliberately not a compile-time one: the font is not shipped with the
# program, so what a machine can draw is a question for `HasFluentIcons()` / `HasGlyph()` at run
# time. This is the list a page has to have a fallback for, and it is the list the audit exists to
# keep complete.
$notInMdl2 = @($stated | Where-Object { -not $mdl2.ContainsKey($_.code) })
Write-Host ''
Write-Host "of the header's $($stated.Count), $($notInMdl2.Count) are not in Segoe MDL2 Assets; a page that draws one of these on Windows 10 needs a fallback:"
$notInMdl2 | Sort-Object code | ForEach-Object { '  {0:X4}  {1}' -f $_.code, $_.name }

if (-not $WriteNames) {
    Write-Host ''
    if ($stale.Count) {
        Write-Host "GLYPH-NAMES.CSV IS NOT THE COPY OF THESE TABLES ($($stale.Count)) -- run this with -WriteNames:"
        $stale | ForEach-Object { $_ }
    } else {
        Write-Host "glyph-names.csv is the copy of these tables: $($expected.Count) code points"
    }
}

if ($wrong.Count -or $gaps.Count -or $stale.Count) { exit 1 }
