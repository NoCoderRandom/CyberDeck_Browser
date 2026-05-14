param(
    [string]$Version = "0.1.0",
    [string]$CefRoot = "",
    [string]$CefUrl = "https://cef-builds.spotifycdn.com/cef_binary_147.0.10%2Bgd58e84d%2Bchromium-147.0.7727.118_windows64.tar.bz2",
    [string]$BuildDir = "build-windows-release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Architecture = "x64",
    [string]$OutputDir = "dist\release-assets",
    [string]$StagingRoot = "dist\release-staging",
    [string]$IsccPath = "",
    [string]$CefSha256 = "",
    [switch]$SkipDownload,
    [switch]$SkipInstaller,
    [switch]$SkipMediaProbe,
    [switch]$CodecEnabledCef,
    [switch]$AcceptCodecResponsibility
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return Join-Path $script:RepoRoot $Path
}

function Get-ArchiveNameFromUrl {
    param([string]$Url)

    $uri = [System.Uri]$Url
    $name = [System.IO.Path]::GetFileName($uri.AbsolutePath)
    return [System.Uri]::UnescapeDataString($name)
}

function Get-CefRootFromArchiveName {
    param([string]$ArchiveName)

    $name = $ArchiveName
    foreach ($extension in @(".tar.bz2", ".tar.gz", ".zip", ".7z")) {
        if ($name.EndsWith($extension, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $name.Substring(0, $name.Length - $extension.Length)
        }
    }

    return [System.IO.Path]::GetFileNameWithoutExtension($name)
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

    $fullPath = Normalize-DirectoryPath -Path $Path
    $root = Normalize-DirectoryPath -Path $AllowedRoot
    $separator = [IO.Path]::DirectorySeparatorChar
    $rootWithSeparator = $root.TrimEnd($separator) + $separator

    if ($fullPath -eq $root -or $fullPath.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath
    }

    throw "Refusing to operate on untrusted path: $Path. Allowed base is $AllowedRoot."
}

function Clean-CefHash {
    param([string]$Hash)
    if ([string]::IsNullOrWhiteSpace($Hash)) {
        return ""
    }

    return ($Hash -replace '[^0-9A-Fa-f]', "").ToLowerInvariant()
}

function Extract-Sha256Token {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $null
    }

    $match = [regex]::Match($Text, "(?i)\b([0-9a-f]{64})\b")
    if (!$match.Success) {
        return $null
    }

    return $match.Groups[1].Value.ToLowerInvariant()
}

function Resolve-CefExpectedSha256 {
    param(
        [string]$ArchiveName,
        [string]$ArchivePath,
        [string]$ArchiveUrl,
        [string]$ProvidedHash
    )

    if (-not [string]::IsNullOrWhiteSpace($ProvidedHash)) {
        $normalized = Clean-CefHash -Hash $ProvidedHash
        if ($normalized.Length -ne 64) {
            throw "The provided -CefSha256 value is not a valid SHA-256 hash: $ProvidedHash"
        }
        return $normalized
    }

    $manifestPath = Join-Path $script:RepoRoot "scripts\cef_windows_downloads.sha256"
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        foreach ($line in Get-Content -LiteralPath $manifestPath) {
            $entry = $line.Trim()
            if ($entry.Length -eq 0 -or $entry.StartsWith("#")) {
                continue
            }

            $parts = $entry -split "\s+"
            if ($parts.Length -ge 2 -and $parts[0] -ieq $ArchiveName) {
                $candidate = Clean-CefHash -Hash $parts[1]
                if ($candidate.Length -eq 64) {
                    return $candidate
                }
            } elseif ($parts.Length -ge 2 -and $parts[$parts.Length - 1] -ieq $ArchiveName) {
                $candidate = Clean-CefHash -Hash $parts[0]
                if ($candidate.Length -eq 64) {
                    return $candidate
                }
            }
        }
    }

    $archiveSidecar = "${ArchivePath}.sha256"
    if (Test-Path -LiteralPath $archiveSidecar -PathType Leaf) {
        $candidate = Extract-Sha256Token -Text (Get-Content -Raw -LiteralPath $archiveSidecar)
        if ($null -ne $candidate) {
            return $candidate
        }
    }

    try {
        $checksumUrl = "$ArchiveUrl.sha256"
        Write-Host "No local checksum available; fetching manifest from $checksumUrl"
        $checksumResponse = Invoke-WebRequest -Uri $checksumUrl -UseBasicParsing -ErrorAction Stop
        $candidate = Extract-Sha256Token -Text $checksumResponse.Content
        if ($null -ne $candidate) {
            return $candidate
        }
    } catch {
        throw "Could not resolve SHA-256 checksum for '$ArchiveName'. Pass -CefSha256 with a trusted hash."
    }

    throw "Could not resolve a SHA-256 checksum for '$ArchiveName'. Pass -CefSha256 with a trusted hash."
}

function Get-FileSha256 {
    param([string]$Path)
    try {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    } catch {
        return [BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash([System.IO.File]::ReadAllBytes($Path))).ToLowerInvariant().Replace("-", "")
    }
}

function Verify-CefArchive {
    param(
        [string]$ArchivePath,
        [string]$ExpectedSha256
    )

    $expected = Clean-CefHash -Hash $ExpectedSha256
    if ($expected.Length -ne 64) {
        throw "Invalid expected SHA-256 hash length for '$ArchivePath'."
    }

    $actual = Get-FileSha256 -Path $ArchivePath
    if ($actual -ne $expected) {
        throw "SHA-256 check failed for '$ArchivePath'. Expected $expected but got $actual."
    }

    Write-Host "Verified CEF archive checksum: $([System.IO.Path]::GetFileName($ArchivePath))"
}

function Download-File {
    param(
        [string]$Url,
        [string]$Destination,
        [string]$ExpectedSha256
    )

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $existing = Get-FileSha256 -Path $Destination
        if ($existing -eq (Clean-CefHash -Hash $ExpectedSha256)) {
            Write-Host "CEF archive already exists and matches checksum: $Destination"
            return
        }

        Write-Host "Existing CEF archive checksum mismatch; redownloading: $Destination"
        Remove-Item -LiteralPath $Destination -Force
    }

    Write-Host "Downloading CEF:"
    Write-Host "  $Url"

    $curl = Get-Command "curl.exe" -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        & $curl.Source -L --fail --output $Destination $Url
        if ($LASTEXITCODE -ne 0) {
            throw "curl failed while downloading CEF."
        }
        Verify-CefArchive -ArchivePath $Destination -ExpectedSha256 $ExpectedSha256
        return
    }

    Invoke-WebRequest -Uri $Url -OutFile $Destination
    Verify-CefArchive -ArchivePath $Destination -ExpectedSha256 $ExpectedSha256
}

function Find-CSharpCompiler {
    $compiler = Get-Command "csc.exe" -ErrorAction SilentlyContinue
    if ($null -ne $compiler) {
        return $compiler.Source
    }

    foreach ($candidate in @(
        "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
        "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    return $null
}

function Expand-CefArchive {
    param(
        [string]$ArchivePath,
        [string]$DestinationRoot
    )

    $archiveName = Split-Path -Leaf $ArchivePath
    $cefDirectoryName = Get-CefRootFromArchiveName -ArchiveName $archiveName
    $cefDirectory = Join-Path $DestinationRoot $cefDirectoryName
    if (Test-Path -LiteralPath (Join-Path $cefDirectory "include\cef_version.h") -PathType Leaf) {
        Write-Host "CEF already extracted: $cefDirectory"
        return $cefDirectory
    }

    Write-Host "Extracting CEF archive..."
    New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null

    if ($archiveName.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DestinationRoot -Force
    } else {
        $tar = Get-Command "tar.exe" -ErrorAction SilentlyContinue
        if ($null -eq $tar) {
            throw "tar.exe is required to extract '$archiveName'. Install Windows tar support or extract the CEF archive manually and pass -CefRoot."
        }
        & $tar.Source -xf $ArchivePath -C $DestinationRoot
        if ($LASTEXITCODE -ne 0) {
            throw "tar failed while extracting CEF."
        }
    }

    if (!(Test-Path -LiteralPath (Join-Path $cefDirectory "include\cef_version.h") -PathType Leaf)) {
        throw "Extracted CEF directory was not found or is invalid: $cefDirectory"
    }

    return $cefDirectory
}

function Resolve-CefRoot {
    if ($CefRoot.Trim().Length -gt 0) {
        $resolved = Resolve-RepoPath -Path $CefRoot
        if (!(Test-Path -LiteralPath (Join-Path $resolved "include\cef_version.h") -PathType Leaf)) {
            throw "CEF_ROOT is invalid: $resolved"
        }
        return (Resolve-Path -LiteralPath $resolved).Path
    }

    if ($SkipDownload) {
        throw "-SkipDownload requires -CefRoot."
    }

    $archiveName = Get-ArchiveNameFromUrl -Url $CefUrl
    $downloadDir = Join-Path $script:RepoRoot "third_party\cef-downloads"
    $extractRoot = Join-Path $script:RepoRoot "third_party"
    $archivePath = Join-Path $downloadDir $archiveName
    $expectedHash = Resolve-CefExpectedSha256 -ArchiveName $archiveName -ArchivePath $archivePath -ArchiveUrl $CefUrl -ProvidedHash $CefSha256
    Write-Host "Resolved CEF SHA-256 checksum from trusted metadata."
    New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
    Download-File -Url $CefUrl -Destination $archivePath -ExpectedSha256 $expectedHash
    return Expand-CefArchive -ArchivePath $archivePath -DestinationRoot $extractRoot
}

function Copy-AppRuntime {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (Test-Path -LiteralPath $DestinationDir) {
        Remove-Item -LiteralPath $DestinationDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Recurse -Force
    }
}

function Write-PortableLauncher {
    param([string]$PortableRoot)

    $launcherPath = Join-Path $PortableRoot "CyberDeckBrowserPortable.cmd"
    $launcher = @"
@echo off
setlocal
set "CYBERDECK_APPDATA_DIR=%~dp0Data"
if not exist "%CYBERDECK_APPDATA_DIR%" mkdir "%CYBERDECK_APPDATA_DIR%"
start "" "%~dp0App\CyberDeckBrowser.exe" %*
"@
    Set-Content -LiteralPath $launcherPath -Value $launcher -Encoding ASCII

    $compiler = Find-CSharpCompiler
    if ($null -eq $compiler) {
        Write-Warning "C# compiler not found; portable EXE launcher was not created."
        return
    }

    $sourcePath = Join-Path $script:RepoRoot "scripts\CyberDeckPortableLauncher.cs"
    $exePath = Join-Path $PortableRoot "CyberdeckPortable.exe"
    $iconPath = Join-Path $PortableRoot "CyberdeckPortable.ico"
    $repoIconPath = Join-Path $script:RepoRoot "assets\windows\CyberdeckPortable.ico"
    if (Test-Path -LiteralPath $repoIconPath -PathType Leaf) {
        Copy-Item -LiteralPath $repoIconPath -Destination $iconPath -Force
    }

    $compileArgs = @(
        "/nologo",
        "/target:winexe",
        "/platform:x64",
        "/optimize+",
        "/reference:System.Windows.Forms.dll",
        "/out:$exePath",
        $sourcePath
    )
    if (Test-Path -LiteralPath $iconPath -PathType Leaf) {
        $compileArgs = @("/win32icon:$iconPath") + $compileArgs
    }

    & $compiler @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Portable EXE launcher compilation failed."
    }
}

function New-PortablePackage {
    param(
        [string]$InstallerAppDir,
        [string]$PortableRoot,
        [string]$ZipPath
    )

    if (Test-Path -LiteralPath $PortableRoot) {
        Remove-Item -LiteralPath $PortableRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $PortableRoot "App") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $PortableRoot "Data") | Out-Null
    Copy-AppRuntime -SourceDir $InstallerAppDir -DestinationDir (Join-Path $PortableRoot "App")
    Write-PortableLauncher -PortableRoot $PortableRoot

    foreach ($file in @("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md")) {
        $source = Join-Path $script:RepoRoot $file
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $PortableRoot -Force
        }
    }

    foreach ($doc in @(
        "docs\USER_GUIDE.md",
        "docs\WINDOWS_MEDIA.md",
        "docs\PACKAGING.md",
        "docs\QA_CHECKLIST.md",
        "docs\RELEASE_NOTES_TEMPLATE.md"
    )) {
        $source = Join-Path $script:RepoRoot $doc
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $docDestination = Join-Path $PortableRoot $doc
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $docDestination) | Out-Null
            Copy-Item -LiteralPath $source -Destination $docDestination -Force
        }
    }

    Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot "docs") -Filter "RELEASE_NOTES_v*.md" -File |
        ForEach-Object {
            $docDestination = Join-Path (Join-Path $PortableRoot "docs") $_.Name
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $docDestination) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $docDestination -Force
        }

    if (Test-Path -LiteralPath $ZipPath -PathType Leaf) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    Compress-Archive -LiteralPath $PortableRoot -DestinationPath $ZipPath -Force
}

$script:RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$outputPath = Resolve-RepoPath -Path $OutputDir
$stagingPath = Assert-PathUnder -Path (Resolve-RepoPath -Path $StagingRoot) -AllowedRoot (Join-Path $script:RepoRoot "dist")
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
New-Item -ItemType Directory -Force -Path $stagingPath | Out-Null

if ($CodecEnabledCef -and !$AcceptCodecResponsibility) {
    throw "Pass -AcceptCodecResponsibility when using -CodecEnabledCef. H.264/AAC distribution can require patent/license clearance."
}

if (!$CodecEnabledCef) {
    Write-Warning "Default official CEF builds may not support H.264/AAC. For Reddit and broad YouTube playback, pass -CefRoot pointing at a codec-enabled CEF build and use -CodecEnabledCef -AcceptCodecResponsibility."
}

$resolvedCefRoot = Resolve-CefRoot
Write-Host "Using CEF_ROOT: $resolvedCefRoot"

& (Join-Path $script:RepoRoot "scripts\build_release.ps1") `
    -BuildDir $BuildDir `
    -CefRoot $resolvedCefRoot `
    -RequireCef `
    -Generator $Generator `
    -Architecture $Architecture

$packageArgs = @{
    BuildDir = $BuildDir
    Configuration = "Release"
    PackageDir = "dist\installer-staging"
    OutputDir = $OutputDir
    Version = $Version
}
if ($IsccPath.Trim().Length -gt 0) {
    $packageArgs.IsccPath = $IsccPath
}
if ($SkipInstaller) {
    $packageArgs.SkipCompile = $true
}

& (Join-Path $script:RepoRoot "scripts\package_installer.ps1") @packageArgs

$installerAppDir = Join-Path $script:RepoRoot "dist\installer-staging\app"
& (Join-Path $script:RepoRoot "scripts\verify_windows_media_runtime.ps1") -AppDir $installerAppDir

$portableName = "CyberDeckBrowser-$Version-portable-win64"
$portableRoot = Join-Path $stagingPath $portableName
$portableZip = Join-Path $outputPath "$portableName.zip"
New-PortablePackage -InstallerAppDir $installerAppDir -PortableRoot $portableRoot -ZipPath $portableZip
& (Join-Path $script:RepoRoot "scripts\verify_windows_media_runtime.ps1") -AppDir (Join-Path $portableRoot "App")

Write-Host ""
Write-Host "Windows release build complete."
Write-Host "Portable zip: $portableZip"
if (!$SkipInstaller) {
    Write-Host "Installer output directory: $outputPath"
}
if (!$CodecEnabledCef) {
    Write-Warning "The release was built with a CEF path that was not marked codec-enabled. Do not claim Reddit/H.264/AAC playback support until media QA passes."
} elseif (!$SkipMediaProbe) {
    & (Join-Path $script:RepoRoot "scripts\test_windows_media_playback.ps1") `
        -AppExe (Join-Path $script:RepoRoot (Join-Path $BuildDir "Release\CyberDeckBrowser.exe"))
}
