param(
    [string]$AppExe = "build-windows-release\Release\CyberDeckBrowser.exe",
    [int]$Port = 8765,
    [int]$TimeoutSeconds = 30,
    [switch]$KeepBrowserOpen
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    $repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return Join-Path $repoRoot $Path
}

function New-ProbeHtml {
    return @'
<!doctype html>
<meta charset="utf-8">
<title>CyberDeck Media Probe</title>
<style>
html, body { background: #000; color: #00ff00; font: 16px Consolas, monospace; }
main { max-width: 900px; margin: 32px auto; border: 1px solid #00ff00; padding: 24px; }
.warn { color: #ffff00; }
.bad { color: #ff4040; }
</style>
<main>
<h1>CyberDeck Media Probe</h1>
<pre id="out">Running media capability checks...</pre>
</main>
<script>
(async function(){
  const video = document.createElement('video');
  const audio = document.createElement('audio');
  const results = {
    userAgent: navigator.userAgent,
    h264: video.canPlayType('video/mp4; codecs="avc1.42E01E"'),
    h264High: video.canPlayType('video/mp4; codecs="avc1.640028"'),
    aac: audio.canPlayType('audio/mp4; codecs="mp4a.40.2"'),
    mp4: video.canPlayType('video/mp4'),
    webmVp8: video.canPlayType('video/webm; codecs="vp8, vorbis"'),
    webmVp9: video.canPlayType('video/webm; codecs="vp9, opus"'),
    webmAv1: video.canPlayType('video/webm; codecs="av01.0.05M.08, opus"'),
    mse: !!(window.MediaSource || window.ManagedMediaSource),
    eme: !!navigator.requestMediaKeySystemAccess
  };
  document.getElementById('out').textContent = JSON.stringify(results, null, 2);
  const encoded = encodeURIComponent(JSON.stringify(results));
  await fetch('/result?payload=' + encoded, { mode: 'no-cors' }).catch(function(){});
})();
</script>
'@
}

$resolvedApp = Resolve-RepoPath -Path $AppExe
if (!(Test-Path -LiteralPath $resolvedApp -PathType Leaf)) {
    throw "CyberDeckBrowser.exe was not found: $resolvedApp"
}

$listener = [System.Net.HttpListener]::new()
$prefix = "http://127.0.0.1:$Port/"
$listener.Prefixes.Add($prefix)
$listener.Start()

$result = $null
$browserProcess = $null
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

try {
    Write-Host "Starting media probe server: $prefix"
    Write-Host "Launching CyberDeck Browser: $resolvedApp"
    $browserProcess = Start-Process -FilePath $resolvedApp -ArgumentList $prefix -PassThru

    while ([DateTime]::UtcNow -lt $deadline -and $null -eq $result) {
        $async = $listener.BeginGetContext($null, $null)
        while (!$async.AsyncWaitHandle.WaitOne(100)) {
            if ([DateTime]::UtcNow -ge $deadline) {
                throw "Timed out waiting for CyberDeck to report media capabilities."
            }
        }

        $context = $listener.EndGetContext($async)
        $request = $context.Request
        $response = $context.Response

        if ($request.Url.AbsolutePath -eq "/result") {
            $payload = $request.QueryString["payload"]
            if ($payload) {
                $result = [System.Uri]::UnescapeDataString($payload) | ConvertFrom-Json
            }
            $bytes = [System.Text.Encoding]::UTF8.GetBytes("ok")
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
            $response.Close()
            break
        }

        $html = New-ProbeHtml
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($html)
        $response.ContentType = "text/html; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
    }
} finally {
    if ($listener.IsListening) {
        $listener.Stop()
    }
    $listener.Close()

    if (!$KeepBrowserOpen -and $null -ne $browserProcess -and !$browserProcess.HasExited) {
        Stop-Process -Id $browserProcess.Id -Force
    }
}

if ($null -eq $result) {
    throw "No media probe result was reported."
}

Write-Host "Media capability result:"
$result | Format-List

$h264Works = $result.h264 -eq "probably" -or $result.h264 -eq "maybe" -or
    $result.h264High -eq "probably" -or $result.h264High -eq "maybe"
$aacWorks = $result.aac -eq "probably" -or $result.aac -eq "maybe"

if (!$h264Works -or !$aacWorks) {
    throw "H.264/AAC probe failed. This build is not ready to ship as Reddit/MP4 video-capable."
}

Write-Host "H.264/AAC probe passed. Continue with real YouTube and Reddit playback QA."
