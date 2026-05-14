param(
    [string]$AppDir = "dist\installer-staging\app",
    [switch]$AllowPlaceholder
)

$ErrorActionPreference = "Stop"

function Resolve-AppDir {
    param([string]$Path)

    $root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
    $candidate = $Path
    if (![System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $root $candidate
    }

    if (!(Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "App directory was not found: $candidate"
    }

    return (Resolve-Path -LiteralPath $candidate).Path
}

function Assert-FilePresent {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )

    $path = Join-Path $BaseDir $RelativePath
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing required runtime file: $RelativePath"
    }

    $size = (Get-Item -LiteralPath $path).Length
    Write-Host ("OK file: {0} ({1:N0} bytes)" -f $RelativePath, $size)
}

function Assert-DirectoryPresent {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )

    $path = Join-Path $BaseDir $RelativePath
    if (!(Test-Path -LiteralPath $path -PathType Container)) {
        throw "Missing required runtime directory: $RelativePath"
    }

    Write-Host "OK dir:  $RelativePath"
}

$resolvedAppDir = Resolve-AppDir -Path $AppDir
Write-Host "Checking Windows app runtime: $resolvedAppDir"

Assert-FilePresent -BaseDir $resolvedAppDir -RelativePath "CyberDeckBrowser.exe"

$cefPath = Join-Path $resolvedAppDir "libcef.dll"
if (!(Test-Path -LiteralPath $cefPath -PathType Leaf)) {
    if ($AllowPlaceholder) {
        Write-Warning "libcef.dll is missing. This is a placeholder/non-CEF package."
        exit 0
    }

    throw "libcef.dll is missing. This package cannot be a functional CEF browser release."
}

foreach ($requiredFile in @(
    "libcef.dll",
    "chrome_elf.dll",
    "icudtl.dat",
    "resources.pak",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "v8_context_snapshot.bin",
    "libEGL.dll",
    "libGLESv2.dll",
    "d3dcompiler_47.dll",
    "vk_swiftshader.dll",
    "vk_swiftshader_icd.json",
    "vulkan-1.dll"
)) {
    Assert-FilePresent -BaseDir $resolvedAppDir -RelativePath $requiredFile
}

Assert-DirectoryPresent -BaseDir $resolvedAppDir -RelativePath "locales"
Assert-FilePresent -BaseDir $resolvedAppDir -RelativePath "locales\en-US.pak"

foreach ($recommendedFile in @("dxcompiler.dll", "dxil.dll", "bootstrap.exe", "bootstrapc.exe", "chrome_crashpad_handler.exe")) {
    $path = Join-Path $resolvedAppDir $recommendedFile
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $size = (Get-Item -LiteralPath $path).Length
        Write-Host ("OK optional: {0} ({1:N0} bytes)" -f $recommendedFile, $size)
    } else {
        Write-Warning "Recommended CEF helper file is missing: $recommendedFile"
    }
}

Write-Host ""
Write-Host "Runtime file check passed."
Write-Warning "This file check does not prove H.264/AAC codec support."
Write-Host "For Reddit and broad YouTube playback, verify the CEF build itself was built with proprietary codecs enabled, then run the media smoke tests in docs\WINDOWS_MEDIA.md."
