param(
    [string]$BuildDir = "build-release",
    [string]$CefRoot = "",
    [switch]$RequireCef,
    [string]$Generator = "",
    [string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $repoRoot $BuildDir

$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildPath,
    "-DCMAKE_BUILD_TYPE=Release"
)

if ($Generator.Trim().Length -gt 0) {
    $generatorArgs = @("-G", $Generator)
    if ($Architecture.Trim().Length -gt 0) {
        $generatorArgs += @("-A", $Architecture)
    }
    $configureArgs = $generatorArgs + $configureArgs
}

if ($CefRoot.Trim().Length -gt 0) {
    $configureArgs += "-DCEF_ROOT=$CefRoot"
}

if ($RequireCef) {
    $configureArgs += "-DCYBERDECK_REQUIRE_CEF=ON"
}

Write-Host "Configuring CyberDeck Browser release build..."
cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building CyberDeck Browser release binaries..."
cmake --build $buildPath --config Release --target CyberDeckBrowser
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Host "Release build complete: $buildPath"
