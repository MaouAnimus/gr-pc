param(
    [switch]$Headless,
    [switch]$Strict,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$savedPath = $env:PATH
$runtimeArguments = @()
if ($Headless) { $runtimeArguments += '--headless' }
if ($Strict) { $runtimeArguments += '--strict' }
try {
    $env:PATH = "C:\msys64\clang64\bin;$savedPath"
    Push-Location -LiteralPath $projectRoot
    try {
        & rtk proxy "./out/$($Configuration.ToLowerInvariant())/launcher.exe" @runtimeArguments
        $runtimeExitCode = $LASTEXITCODE
    } finally { Pop-Location }
} finally { $env:PATH = $savedPath }
exit $runtimeExitCode
