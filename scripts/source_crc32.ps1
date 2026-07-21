param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)

if (-not ('TphdTools.SourceCrc32' -as [type])) {
    Add-Type -TypeDefinition @'
using System;

namespace TphdTools
{
    public static class SourceCrc32
    {
        private static readonly uint[] Table = BuildTable();

        private static uint[] BuildTable()
        {
            var table = new uint[256];
            for (uint i = 0; i < table.Length; ++i)
            {
                uint value = i;
                for (int bit = 0; bit < 8; ++bit)
                    value = (value >> 1) ^ ((value & 1) != 0 ? 0xEDB88320u : 0u);
                table[i] = value;
            }
            return table;
        }

        public static uint Update(uint crc, byte[] bytes)
        {
            foreach (byte value in bytes)
                crc = (crc >> 8) ^ Table[(crc ^ value) & 0xFF];
            return crc;
        }
    }
}
'@
}

$files = [Collections.Generic.List[IO.FileInfo]]::new()

function Add-TreeFiles([string]$relativeDirectory, [string[]]$extensions = @()) {
    $directory = Join-Path $ProjectRoot $relativeDirectory
    if (Test-Path -LiteralPath $directory -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse) {
            if ($extensions.Count -eq 0 -or $file.Extension -in $extensions) {
                $files.Add($file)
            }
        }
    }
}

$sourceExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.s')
Add-TreeFiles 'src' $sourceExtensions
Add-TreeFiles 'include' $sourceExtensions
Add-TreeFiles 'data'
Add-TreeFiles 'external\cjson' @('.c', '.h')
Add-TreeFiles 'external\imgui\backends\wiiu' @('.c', '.cpp', '.h')
Add-TreeFiles 'graphicspack' @('.asm', '.txt')

$imguiRoot = Join-Path $ProjectRoot 'external\imgui'
if (Test-Path -LiteralPath $imguiRoot -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $imguiRoot -File) {
        if ($file.Extension -in @('.c', '.cpp', '.h')) {
            $files.Add($file)
        }
    }
}

foreach ($relativeFile in @('Makefile', 'Makefile.aroma', 'exports.def')) {
    $path = Join-Path $ProjectRoot $relativeFile
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $files.Add((Get-Item -LiteralPath $path))
    }
}

if ($files.Count -eq 0) {
    throw "No build inputs were found under '$ProjectRoot'."
}

$relativePaths = [string[]]@(
    $files |
        ForEach-Object {
            $_.FullName.Substring($ProjectRoot.Length).TrimStart('\', '/').Replace('\', '/')
        } |
        Sort-Object -Unique
)
[Array]::Sort($relativePaths, [StringComparer]::Ordinal)

$crc = [uint32]::MaxValue
$separator = [byte[]]@(0)
$utf8 = [Text.UTF8Encoding]::new($false)

foreach ($relativePath in $relativePaths) {
    $crc = [TphdTools.SourceCrc32]::Update($crc, $utf8.GetBytes($relativePath))
    $crc = [TphdTools.SourceCrc32]::Update($crc, $separator)
    $crc = [TphdTools.SourceCrc32]::Update(
        $crc,
        [IO.File]::ReadAllBytes((Join-Path $ProjectRoot $relativePath))
    )
    $crc = [TphdTools.SourceCrc32]::Update($crc, $separator)
}

$crc = $crc -bxor [uint32]::MaxValue
'CRC32-{0:X8}' -f $crc
