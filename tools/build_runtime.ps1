param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$ToolchainBin = 'C:\msys64\clang64\bin',
    [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "out\$($Configuration.ToLowerInvariant())"
$savedPath = $env:PATH
try {
    foreach ($tool in @('cmake.exe', 'ninja.exe', 'clang++.exe', 'ctest.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $ToolchainBin $tool))) {
            throw "Missing tool: $ToolchainBin\$tool"
        }
    }
    $env:PATH = "$ToolchainBin;$savedPath"
    $configureArguments = @()
    if ($Fresh) { $configureArguments += '--fresh' }
    & rtk proxy (Join-Path $ToolchainBin 'cmake.exe') @configureArguments -S $projectRoot -B $buildRoot -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$ToolchainBin\clang++.exe" "-DCMAKE_MAKE_PROGRAM=$ToolchainBin\ninja.exe" -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & rtk proxy (Join-Path $ToolchainBin 'cmake.exe') --build $buildRoot
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & rtk proxy (Join-Path $ToolchainBin 'ctest.exe') --test-dir $buildRoot --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Runtime tests failed.' }
    # RTK resolves an executable path containing spaces incorrectly on Windows.
    Push-Location -LiteralPath $buildRoot
    try {
        & rtk proxy ./gravity_runtime_probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime probe failed.' }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $savedPath
}
