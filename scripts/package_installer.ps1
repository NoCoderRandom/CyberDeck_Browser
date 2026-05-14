param(
    [string]$BuildDir = "build-release",
    [string]$Configuration = "Release",
    [string]$PackageDir = "dist\installer-staging",
    [string]$OutputDir = "dist",
    [string]$IsccPath = "",
    [string]$Version = "0.1.0",
    [switch]$SkipCompile,
    [switch]$AllowPlaceholder
)

$ErrorActionPreference = "Stop"

function Resolve-Iscc {
    param([string]$ExplicitPath)

    if ($ExplicitPath.Trim().Length -gt 0) {
        if (!(Test-Path -LiteralPath $ExplicitPath)) {
            throw "ISCC.exe was not found at '$ExplicitPath'."
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $command = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "Inno Setup compiler ISCC.exe was not found. Install Inno Setup 6 or pass -IsccPath."
}

function Normalize-DirectoryPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path is empty."
    }

    return ([System.IO.Path]::GetFullPath($Path)).TrimEnd('\', '/')
}

function Assert-PathUnder {
    param(
        [string]$Path,
        [string]$AllowedRoot
    )

    $full_path = Normalize-DirectoryPath -Path $Path
    $root = Normalize-DirectoryPath -Path $AllowedRoot
    $separator = [IO.Path]::DirectorySeparatorChar
    $root_with_separator = $root.TrimEnd($separator) + $separator

    if ($full_path -eq $root -or $full_path.StartsWith($root_with_separator, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full_path
    }

    throw "Refusing to operate on untrusted path: $Path. Allowed base is $AllowedRoot."
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return Join-Path $repoRoot $Path
}

function Copy-FilePattern {
    param(
        [string]$SourceDir,
        [string]$Pattern,
        [string]$DestinationDir
    )

    Get-ChildItem -LiteralPath $SourceDir -File -Filter $Pattern -ErrorAction SilentlyContinue |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Force
        }
}

function Copy-DirectoryIfPresent {
    param(
        [string]$SourceDir,
        [string]$Name,
        [string]$DestinationDir
    )

    $source = Join-Path $SourceDir $Name
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $DestinationDir $Name) -Recurse -Force
    }
}

function Assert-FilePresent {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )

    $path = Join-Path $BaseDir $RelativePath
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required runtime file is missing from installer staging: $RelativePath"
    }
}

function Assert-DirectoryPresent {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )

    $path = Join-Path $BaseDir $RelativePath
    if (!(Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required runtime directory is missing from installer staging: $RelativePath"
    }
}

function Test-StagedRuntime {
    param(
        [string]$AppStage,
        [bool]$AllowPlaceholderBuild
    )

    Assert-FilePresent -BaseDir $AppStage -RelativePath "CyberDeckBrowser.exe"

    $cefMarker = Join-Path $AppStage "libcef.dll"
    if (!(Test-Path -LiteralPath $cefMarker -PathType Leaf)) {
        if ($AllowPlaceholderBuild) {
            Write-Warning "libcef.dll was not found. Packaging a placeholder/non-CEF build because -AllowPlaceholder was supplied."
            return
        }
        throw "libcef.dll was not found in the staged app. Build with -CefRoot and -RequireCef, or pass -AllowPlaceholder intentionally."
    }

    foreach ($requiredFile in @(
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
        Assert-FilePresent -BaseDir $AppStage -RelativePath $requiredFile
    }

    Assert-DirectoryPresent -BaseDir $AppStage -RelativePath "locales"
    Assert-FilePresent -BaseDir $AppStage -RelativePath "locales\en-US.pak"

    foreach ($recommendedFile in @("dxcompiler.dll", "dxil.dll", "bootstrap.exe", "bootstrapc.exe", "chrome_crashpad_handler.exe")) {
        $path = Join-Path $AppStage $recommendedFile
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            Write-Warning "Recommended CEF helper file is not staged: $recommendedFile"
        }
    }

    Write-Host "CEF runtime staging check passed."
    Write-Warning "This check cannot prove H.264/AAC support. Reddit and many YouTube streams require a CEF/Chromium build with proprietary codecs enabled and the related licenses cleared."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildRoot = Join-Path $repoRoot $BuildDir
$singleConfigExe = Join-Path $buildRoot "CyberDeckBrowser.exe"
$multiConfigExe = Join-Path (Join-Path $buildRoot $Configuration) "CyberDeckBrowser.exe"

if (Test-Path -LiteralPath $multiConfigExe) {
    $runtimeDir = Split-Path -Parent $multiConfigExe
} elseif (Test-Path -LiteralPath $singleConfigExe) {
    $runtimeDir = Split-Path -Parent $singleConfigExe
} else {
    throw "CyberDeckBrowser.exe was not found in '$buildRoot'. Run scripts\build_release.ps1 first."
}

$stageRoot = Assert-PathUnder -Path (Resolve-RepoPath -Path $PackageDir) -AllowedRoot (Join-Path $repoRoot "dist")
$appStage = Join-Path $stageRoot "app"
$outputPath = Join-Path $repoRoot $OutputDir
$issPath = Join-Path $repoRoot "installer\CyberDeckBrowser.iss"

if (!(Test-Path -LiteralPath $issPath)) {
    throw "Installer script not found: $issPath"
}

Write-Host "Preparing installer staging directory..."
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $appStage | Out-Null
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

Copy-Item -LiteralPath (Join-Path $runtimeDir "CyberDeckBrowser.exe") -Destination $appStage -Force

foreach ($pattern in @("*.dll", "*.pak", "*.dat", "*.bin", "*.json")) {
    Copy-FilePattern -SourceDir $runtimeDir -Pattern $pattern -DestinationDir $appStage
}

foreach ($helperExe in @("bootstrap.exe", "bootstrapc.exe", "chrome_crashpad_handler.exe")) {
    $helperPath = Join-Path $runtimeDir $helperExe
    if (Test-Path -LiteralPath $helperPath -PathType Leaf) {
        Copy-Item -LiteralPath $helperPath -Destination $appStage -Force
    }
}

foreach ($runtimeFolder in @("locales", "swiftshader", "resources", "angledata", "MEIPreload", "WidevineCdm")) {
    Copy-DirectoryIfPresent -SourceDir $runtimeDir -Name $runtimeFolder -DestinationDir $appStage
}

foreach ($file in @("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md")) {
    $source = Join-Path $repoRoot $file
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $appStage -Force
    }
}

$assets = Join-Path $repoRoot "assets"
if (Test-Path -LiteralPath $assets) {
    Copy-Item -LiteralPath $assets -Destination (Join-Path $appStage "assets") -Recurse -Force
}

Test-StagedRuntime -AppStage $appStage -AllowPlaceholderBuild $AllowPlaceholder.IsPresent

if ($SkipCompile) {
    Write-Host "Installer staging complete: $appStage"
    exit 0
}

$iscc = Resolve-Iscc -ExplicitPath $IsccPath

Write-Host "Compiling installer with Inno Setup..."
& $iscc `
    "/DMyAppVersion=$Version" `
    "/DSourceDir=$appStage" `
    "/DOutputDir=$outputPath" `
    $issPath

Write-Host "Installer output directory: $outputPath"
