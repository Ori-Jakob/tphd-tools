$ErrorActionPreference = 'Stop'

$debugBuild = $false
$version = $null

for ($index = 0; $index -lt $args.Count; $index++) {
    $argument = [string]$args[$index]
    switch ($argument.ToLowerInvariant()) {
        '-d'      { $debugBuild = $true }
        '--debug' { $debugBuild = $true }
        '-v' {
            if ($index + 1 -ge $args.Count) {
                throw "Missing version value after '$argument'."
            }
            $version = [string]$args[++$index]
        }
        '--version' {
            if ($index + 1 -ge $args.Count) {
                throw "Missing version value after '$argument'."
            }
            $version = [string]$args[++$index]
        }
        default {
            if ($argument.StartsWith('--version=', [System.StringComparison]::OrdinalIgnoreCase)) {
                $version = $argument.Substring('--version='.Length)
            } else {
                throw "Unknown argument '$argument'. Use -d, --debug, -v, or --version."
            }
        }
    }
}

if ($null -ne $version) {
    if ([string]::IsNullOrWhiteSpace($version)) {
        throw 'Version must not be empty.'
    }
    if ($version.Contains('"') -or $version.Contains("'") -or $version.Contains('\') -or
        $version.Contains("`r") -or $version.Contains("`n")) {
        throw 'Version must not contain quotes, apostrophes, backslashes, or newlines.'
    }
}

$destinations = @(
    'C:\Emulation\Games\WiiU\THE LEGEND OF ZELDA Twilight Princess HD [AZAE]\code'
    'C:\Emulation\Games\WiiU\THE LEGEND OF ZELDA Twilight Princess HD [AZAP]\code'
)

$projectDirectory = $PSScriptRoot
$buildName = if ($debugBuild) { 'debug' } else { 'release' }
$outputFileName = if ($debugBuild) { 'tphd_tools_debug.rpl' } else { 'tphd_tools.rpl' }
$oppositeFileName = if ($debugBuild) { 'tphd_tools.rpl' } else { 'tphd_tools_debug.rpl' }
$outputPath = Join-Path $projectDirectory $outputFileName
$graphicsPackSource = Join-Path $projectDirectory "graphicspack\$buildName"
$graphicsPackDestination =
    Join-Path $env:APPDATA 'Cemu\graphicPacks\TwilightPrincessHD\tphd_tools'

Push-Location $projectDirectory

try {
    Write-Host "Building $buildName RPL: $outputFileName..."

    $makeArguments = @('-B', '-f', 'Makefile')
    if ($null -ne $version) {
        $makeArguments += "VERSION=$version"
        Write-Host "Using version '$version$(if ($debugBuild) { ' DEBUG' })'."
    }
    if ($debugBuild) {
        $makeArguments += 'debug'
    }

    # Native command output is streamed directly to the PowerShell console.
    & make @makeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE. Nothing was installed."
    }

    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw "Build completed, but '$outputPath' was not found."
    }

    foreach ($requiredPackFile in @('patch_overlay.asm', 'rules.txt')) {
        $requiredPath = Join-Path $graphicsPackSource $requiredPackFile
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Graphics-pack file was not found: '$requiredPath'"
        }
    }

    foreach ($destination in $destinations) {
        if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
            throw "Destination directory does not exist: '$destination'"
        }

        $oppositePath = Join-Path $destination $oppositeFileName
        if (Test-Path -LiteralPath $oppositePath -PathType Leaf) {
            Write-Host "Removing old $oppositeFileName from '$destination'..."
            Remove-Item -LiteralPath $oppositePath -Force
        }

        $destinationPath = Join-Path $destination $outputFileName
        Write-Host "Copying $outputFileName to '$destinationPath'..."
        Copy-Item -LiteralPath $outputPath -Destination $destinationPath -Force
    }

    if (-not (Test-Path -LiteralPath $graphicsPackDestination -PathType Container)) {
        Write-Host "Creating graphics-pack directory '$graphicsPackDestination'..."
        New-Item -ItemType Directory -Path $graphicsPackDestination -Force | Out-Null
    }

    foreach ($packFile in @('patch_overlay.asm', 'rules.txt')) {
        $sourcePath = Join-Path $graphicsPackSource $packFile
        $destinationPath = Join-Path $graphicsPackDestination $packFile
        Write-Host "Installing $buildName graphics-pack file '$destinationPath'..."
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
    }

    Write-Host "$buildName build and installation completed successfully."
}
finally {
    Pop-Location
}
