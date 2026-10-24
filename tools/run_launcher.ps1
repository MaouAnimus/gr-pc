param(
    [switch]$Play,
    [switch]$Boot,
    [switch]$Headless,
    [switch]$Strict,
    [uint32]$Timeout = 5000,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$ToolchainBin = 'C:\msys64\clang64\bin'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "out\$($Configuration.ToLowerInvariant())"
$launcherExe = Join-Path $buildRoot "launcher.exe"

if (-not (Test-Path -LiteralPath $launcherExe)) {
    throw "Launcher executable not found at: $launcherExe. Run tools\build_runtime.ps1 first."
}

$savedPath = $env:PATH
try {
    $env:PATH = "$ToolchainBin;$savedPath"
    $argsList = @()
    if ($Play) {
        $argsList += "--play"
    } elseif ($Boot) {
        $argsList += "--boot"
        if ($Timeout -ne 5000) {
            $argsList += "--timeout=$Timeout"
        }
    }
    if ($Headless) { $argsList += "--headless" }
    if ($Strict) { $argsList += "--strict" }

    Push-Location -LiteralPath $projectRoot
    try {
        & $launcherExe @argsList
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $savedPath
}
