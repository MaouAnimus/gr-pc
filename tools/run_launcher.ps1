param(
    [switch]$Headless,
    [switch]$Strict,
    [switch]$Boot,
    [int]$TimeoutMs = 0,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$savedPath = $env:PATH
$runtimeArguments = @()
if ($Headless) { $runtimeArguments += '--headless' }
if ($Strict) { $runtimeArguments += '--strict' }
if ($Boot) { $runtimeArguments += '--boot' }
if ($TimeoutMs -gt 0) { $runtimeArguments += "--timeout=$TimeoutMs" }
try {
    $env:PATH = "C:\msys64\clang64\bin;$savedPath"
    Push-Location -LiteralPath $projectRoot
    try {
        & rtk proxy "./out/$($Configuration.ToLowerInvariant())/launcher.exe" @runtimeArguments
        $runtimeExitCode = $LASTEXITCODE
    } finally { Pop-Location }
} finally { $env:PATH = $savedPath }
exit $runtimeExitCode
