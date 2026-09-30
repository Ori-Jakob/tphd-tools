[CmdletBinding()]
param(
    [string]$Version,
    [string]$DevkitPro,
    [string]$OutDir,
    [switch]$NoBuild,
    [switch]$Clean,
    [Alias('d')]
    [switch]$DebugBuild,
    [Alias('e')]
    [switch]$Experimental,
    [string]$CemuPacks,
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$RepoRoot  = $PSScriptRoot
$LoaderDir = Join-Path $RepoRoot 'external\wii-u-rpl-loader'
$PackDir   = Join-Path $RepoRoot 'Cemu\graphicspack'
$ToolRpl   = Join-Path $RepoRoot 'tphd_tools.rpl'
$LoaderWps = Join-Path $LoaderDir 'rpl_loader.wps'

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'dist' }

function Invoke-Native([string]$exe, [string[]]$argv) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $exe @argv 2>&1
    } catch {
        $global:LASTEXITCODE = 1
        return ''
    } finally {
        $ErrorActionPreference = $prev
    }
    return (($out | Where-Object { $_ -is [string] }) -join "`n")
}

if (-not $Version) {
    $Version = 'dev'
    $sha = Invoke-Native 'git' @('-C', $RepoRoot, 'rev-parse', '--short=8', 'HEAD')
    if ($LASTEXITCODE -eq 0 -and $sha.Trim()) { $Version = $sha.Trim() }
}

$Flavour = ''
if ($Experimental) { $Flavour += '-experimental' }
if ($DebugBuild) { $Flavour += '-debug' }
Write-Host "==> version $Version$Flavour"

if (-not $DevkitPro) {
    $candidates = @()
    if ($env:DEVKITPRO) { $candidates += $env:DEVKITPRO }
    $candidates += 'C:\devkitPro'
    $DevkitPro = $candidates | Where-Object { Test-Path (Join-Path $_ 'msys2\usr\bin\make.exe') } | Select-Object -First 1
    if (-not $DevkitPro) { $DevkitPro = $candidates[-1] }
}
$DevkitPro = $DevkitPro.TrimEnd('\', '/')

function ConvertTo-PosixPath([string]$p) {
    $p = $p -replace '\\', '/'
    if ($p -match '^([A-Za-z]):(.*)$') { return '/' + $Matches[1].ToLower() + $Matches[2] }
    return $p
}

$Make = Join-Path $DevkitPro 'msys2\usr\bin\make.exe'

if (-not $NoBuild) {
    foreach ($req in @(
        @{ Path = $Make;                                          What = 'msys2 make' },
        @{ Path = (Join-Path $DevkitPro 'devkitPPC\bin');         What = 'devkitPPC' },
        @{ Path = (Join-Path $DevkitPro 'wut\share\wut_rules');   What = 'wut' },
        @{ Path = (Join-Path $DevkitPro 'wups\share\wups_rules'); What = 'WUPS' },
        @{ Path = (Join-Path $DevkitPro 'wums\share\libmappedmemory.ld'); What = 'WUMS (libmappedmemory)' }
    )) {
        if (-not (Test-Path $req.Path)) {
            throw "$($req.What) not found at $($req.Path). Set -DevkitPro or `$env:DEVKITPRO."
        }
    }

    $python = $null
    foreach ($name in @('python3', 'python', 'py')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        $major = Invoke-Native $cmd.Source @('-c', 'import sys; print(sys.version_info[0])')
        if ($LASTEXITCODE -eq 0 -and $major.Trim() -eq '3') { $python = $cmd; break }
    }
    if (-not $python) { throw 'No working Python 3 on PATH.' }

    $env:PATH = (@(
        (Join-Path $DevkitPro 'devkitPPC\bin'),
        (Join-Path $DevkitPro 'tools\bin'),
        (Join-Path $DevkitPro 'msys2\usr\bin'),
        $env:PATH
    ) -join ';')
    if (-not $env:TMP)  { $env:TMP  = [System.IO.Path]::GetTempPath() }
    if (-not $env:TEMP) { $env:TEMP = $env:TMP }

    $dkp = ConvertTo-PosixPath $DevkitPro
    $makeVars = @(
        "DEVKITPRO=$dkp",
        "DEVKITPPC=$dkp/devkitPPC",
        "PYTHON=$($python.Name)"
    )

    function Invoke-Make([string]$dir, [string[]]$makeArgs) {
        Push-Location $dir
        try {
            & $Make "-j$Jobs" @makeArgs
            if ($LASTEXITCODE -ne 0) { throw "make failed in $dir (exit $LASTEXITCODE)" }
        } finally { Pop-Location }
    }

    if ($Clean) {
        Write-Host '==> make clean'
        Invoke-Make $LoaderDir (@('clean') + $makeVars)
        Invoke-Make $RepoRoot  (@('clean') + $makeVars)
    }

    Write-Host '==> building rpl_loader.wps'
    Invoke-Make $LoaderDir $makeVars

    Write-Host '==> building tphd_tools.rpl'
    $toolVars = $makeVars + @("VERSION=$Version")
    if ($DebugBuild) { $toolVars += 'DEBUG=1' }
    if ($Experimental) { $toolVars += 'EXPERIMENTAL=1' }
    Invoke-Make $RepoRoot $toolVars
}

foreach ($f in @($ToolRpl, $LoaderWps, (Join-Path $PackDir 'rules.txt'), (Join-Path $PackDir 'patch_tphd_tools.asm'))) {
    if (-not (Test-Path $f)) { throw "missing $f" }
}

$Stage = Join-Path $OutDir "TPHD-Tools$Flavour"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }

$cemuPack    = Join-Path $Stage 'Cemu\TPHD-Tools'
$cemuCode    = Join-Path $cemuPack 'code'
$wiiuPlugins = Join-Path $Stage 'Wii U\wiiu\environments\aroma\plugins'
$wiiuRpls    = Join-Path $Stage 'Wii U\wiiu\rpl-loader\universal'

foreach ($d in @($cemuCode, $wiiuPlugins, $wiiuRpls)) {
    New-Item -ItemType Directory -Force $d | Out-Null
}

Copy-Item (Join-Path $PackDir '*') $cemuPack
Copy-Item $ToolRpl   $cemuCode
Copy-Item $LoaderWps $wiiuPlugins
Copy-Item $ToolRpl   $wiiuRpls

$ZipPath = Join-Path $OutDir "TPHD-Tools-$Version$Flavour.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $prefix = (Resolve-Path $Stage).Path.TrimEnd('\') + '\'
    Get-ChildItem -Path $Stage -Recurse -File | Sort-Object FullName | ForEach-Object {
        $entry = $_.FullName.Substring($prefix.Length) -replace '\\', '/'
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $_.FullName, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        Write-Host "   $entry"
    }
} finally {
    $zip.Dispose()
}
Write-Host "==> wrote $ZipPath"

if ($CemuPacks) {
    if (-not (Test-Path $CemuPacks)) { throw "graphicPacks folder not found at $CemuPacks" }
    $target = Join-Path $CemuPacks 'TPHD-Tools'
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Copy-Item -Recurse $cemuPack $target
    Write-Host "==> installed the Cemu pack to $target"
}
