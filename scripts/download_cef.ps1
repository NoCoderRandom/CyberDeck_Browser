param(
    [Parameter(Mandatory = $true)]
    [string]$CefUrl,

    [string]$Destination = "third_party",
    [string]$ExpectedSha256 = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$destinationPath = Join-Path $root $Destination
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

$archiveName = [System.Uri]::UnescapeDataString((Split-Path -Leaf ([System.Uri]$CefUrl).AbsolutePath))
if (-not $archiveName) {
    throw "Could not determine archive name from URL."
}

$archivePath = Join-Path $destinationPath $archiveName
$manifestPath = Join-Path $root "scripts\cef_windows_downloads.sha256"

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
            throw "The provided -ExpectedSha256 value is not a valid SHA-256 hash: $ProvidedHash"
        }
        return $normalized
    }

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

    $sidecarPath = "${ArchivePath}.sha256"
    if (Test-Path -LiteralPath $sidecarPath -PathType Leaf) {
        $candidate = Extract-Sha256Token -Text (Get-Content -Raw -LiteralPath $sidecarPath)
        if ($null -ne $candidate) {
            return $candidate
        }
    }

    try {
        $checksumUrl = "$ArchiveUrl.sha256"
        $checksumResponse = Invoke-WebRequest -Uri $checksumUrl -UseBasicParsing -ErrorAction Stop
        $candidate = Extract-Sha256Token -Text $checksumResponse.Content
        if ($null -ne $candidate) {
            return $candidate
        }
    } catch {
        throw "Could not resolve SHA-256 checksum for '$archiveName'. Pass -ExpectedSha256 with a trusted hash."
    }

    throw "Could not resolve a SHA-256 checksum for '$archiveName'. Pass -ExpectedSha256 with a trusted hash."
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
}

function Download-CefArchive {
    param([string]$Url, [string]$Destination, [string]$ExpectedSha256)

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $existing = Get-FileHash -Algorithm SHA256 -LiteralPath $Destination -ErrorAction SilentlyContinue
        if ($null -ne $existing -and $existing.Hash.ToLowerInvariant() -eq (Clean-CefHash -Hash $ExpectedSha256)) {
            Write-Host "CEF archive already exists and matches checksum: $Destination"
            return
        }

        Write-Host "Existing CEF archive checksum mismatch; redownloading: $Destination"
        Remove-Item -LiteralPath $Destination -Force
    }

    Write-Host "Downloading CEF from $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Destination
    Verify-CefArchive -ArchivePath $Destination -ExpectedSha256 $ExpectedSha256
}

$expectedHash = Resolve-CefExpectedSha256 -ArchiveName $archiveName -ArchivePath $archivePath -ArchiveUrl $CefUrl -ProvidedHash $ExpectedSha256
if ([string]::IsNullOrWhiteSpace($expectedHash)) {
    throw "Could not resolve trusted SHA-256 checksum for '$archiveName'."
}
Download-CefArchive -Url $CefUrl -Destination $archivePath -ExpectedSha256 $expectedHash

Write-Host "Verified SHA-256 for downloaded archive."

Write-Host "Downloaded to $archivePath"
Write-Host "Extract the archive under third_party, then configure with:"
Write-Host "  cmake -S . -B build -DCEF_ROOT=<path-to-extracted-cef>"
