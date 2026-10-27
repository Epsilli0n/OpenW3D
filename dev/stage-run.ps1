<#
.SYNOPSIS
    Stage a runnable OpenW3D folder next to the build output.

.DESCRIPTION
    Creates <repo>/run/<Preset>-<Config>/ containing the freshly built binaries plus
    links to an existing Renegade install, so the build can be run without copying
    gigabytes of data or overwriting the original 32-bit Bink/Miles DLLs.

    - Directories from the game install (Data, ...) become junctions.
    - Loose files from the game install (fonts, ini files, ...) become hard links
      when on the same volume, otherwise copies.
    - Built binaries (*.exe, *.dll, *.pdb) are copied last and take precedence.

    The game writes saves and config into Data, so those writes land in the linked
    install. Point -GameDir at a copy you are happy to modify.

.EXAMPLE
    ./dev/stage-run.ps1 -GameDir "D:\Games\Renegade"
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$GameDir,
    [string]$Preset = 'win',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "build/$Preset/$Config"
$runDir = Join-Path $repo "run/$Preset-$Config"

if (-not (Test-Path -LiteralPath $buildDir)) { throw "Build output not found: $buildDir" }
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'Data/always.dat'))) {
    throw "GameDir does not look like a Renegade install (Data/always.dat missing): $GameDir"
}

New-Item -ItemType Directory -Force -Path $runDir | Out-Null

# Binaries we build replace these game files, so never link them.
$built = Get-ChildItem -LiteralPath $buildDir -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.pdb' }
$builtNames = @($built | ForEach-Object { $_.Name.ToLowerInvariant() })

$sameVolume = ([IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $GameDir).Path) -eq
               [IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $runDir).Path))

foreach ($item in Get-ChildItem -LiteralPath $GameDir) {
    $dest = Join-Path $runDir $item.Name
    if ($item.PSIsContainer) {
        if (-not (Test-Path -LiteralPath $dest)) {
            New-Item -ItemType Junction -Path $dest -Target $item.FullName | Out-Null
        }
        continue
    }
    # Skip the original executables/DLLs; the run folder only contains our build.
    if ($item.Extension -in '.exe', '.dll' -or $builtNames -contains $item.Name.ToLowerInvariant()) { continue }
    if (Test-Path -LiteralPath $dest) { continue }
    if ($sameVolume) {
        New-Item -ItemType HardLink -Path $dest -Target $item.FullName | Out-Null
    } else {
        Copy-Item -LiteralPath $item.FullName -Destination $dest
    }
}

foreach ($file in $built) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $runDir $file.Name) -Force
}

Write-Output "Staged: $runDir"
