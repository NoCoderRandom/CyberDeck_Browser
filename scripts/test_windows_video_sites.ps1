param(
    [string]$AppExe = "build-windows-release\Release\CyberDeckBrowser.exe",
    [string[]]$Urls = @(
        "https://www.youtube.com/watch?v=jNQXAC9IVRw",
        "https://www.reddit.com/r/videos/"
    ),
    [int]$RemoteDebuggingPort = 9223,
    [int]$PageTimeoutSeconds = 35,
    [int]$PlaybackSeconds = 8,
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

function Wait-JsonEndpoint {
    param(
        [string]$Url,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            return Invoke-RestMethod -Uri $Url -TimeoutSec 2
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }

    throw "Timed out waiting for $Url"
}

function Test-TcpPortOpen {
    param([int]$Port)

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (!$connect.AsyncWaitHandle.WaitOne(250)) {
            return $false
        }
        $client.EndConnect($connect)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Get-PageWebSocketUrl {
    param(
        [int]$Port,
        [int]$TimeoutSeconds = 20
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $targets = Wait-JsonEndpoint -Url "http://127.0.0.1:$Port/json/list" -TimeoutSeconds 2
        $page = @($targets | Where-Object { $_.type -eq "page" -and $_.webSocketDebuggerUrl } | Select-Object -First 1)
        if ($page.Count -gt 0) {
            return $page[0].webSocketDebuggerUrl
        }
        Start-Sleep -Milliseconds 250
    }

    throw "No CEF page target with a DevTools websocket was found."
}

function Receive-WebSocketJson {
    param([System.Net.WebSockets.ClientWebSocket]$Socket)

    $buffer = New-Object byte[] 65536
    $stream = [System.IO.MemoryStream]::new()
    try {
        do {
            $segment = [ArraySegment[byte]]::new($buffer)
            $receive = $Socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($receive.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw "CEF DevTools websocket closed."
            }
            $stream.Write($buffer, 0, $receive.Count)
        } while (!$receive.EndOfMessage)

        $json = [System.Text.Encoding]::UTF8.GetString($stream.ToArray())
        return $json | ConvertFrom-Json
    } finally {
        $stream.Dispose()
    }
}

function Invoke-CdpCommand {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [int]$Id,
        [string]$Method,
        [hashtable]$Params = @{}
    )

    $payload = @{
        id = $Id
        method = $Method
        params = $Params
    } | ConvertTo-Json -Depth 20 -Compress

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $segment = [ArraySegment[byte]]::new($bytes)
    $null = $Socket.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    while ($true) {
        $message = Receive-WebSocketJson -Socket $Socket
        if ($message.id -eq $Id) {
            if ($message.error) {
                throw "CDP command $Method failed: $($message.error.message)"
            }
            return $message.result
        }
    }
}

function Invoke-CdpExpression {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [ref]$NextId,
        [string]$Expression,
        [bool]$AwaitPromise = $false
    )

    $id = $NextId.Value
    $NextId.Value++
    $result = Invoke-CdpCommand -Socket $Socket -Id $id -Method "Runtime.evaluate" -Params @{
        expression = $Expression
        awaitPromise = $AwaitPromise
        returnByValue = $true
    }

    if ($result.exceptionDetails) {
        throw "JavaScript evaluation failed."
    }

    return $result.result.value
}

function Test-VideoUrl {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [ref]$NextId,
        [string]$Url,
        [int]$PageTimeoutSeconds,
        [int]$PlaybackSeconds
    )

    $navigateId = $NextId.Value
    $NextId.Value++
    Invoke-CdpCommand -Socket $Socket -Id $navigateId -Method "Page.navigate" -Params @{ url = $Url } | Out-Null

    $deadline = [DateTime]::UtcNow.AddSeconds($PageTimeoutSeconds)
    Start-Sleep -Seconds 2
    do {
        Start-Sleep -Milliseconds 500
        try {
            $ready = Invoke-CdpExpression -Socket $Socket -NextId $NextId -Expression "document.readyState"
        } catch {
            $ready = ""
        }
        if ($ready -eq "complete" -or $ready -eq "interactive") {
            break
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    $probeScript = @"
(async function(){
  const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));
  const result = {
    requestedUrl: '$($Url.Replace('\', '\\').Replace("'", "\'"))',
    finalUrl: location.href,
    title: document.title,
    hasVideo: false,
    videoCount: 0,
    playResolved: false,
    playError: '',
    before: 0,
    after: 0,
    advanced: false,
    paused: true,
    readyState: 0,
    networkState: 0,
    videoWidth: 0,
    videoHeight: 0
  };
  const start = performance.now();
  while (!document.querySelector('video') && performance.now() - start < $($PageTimeoutSeconds * 1000)) {
    await sleep(500);
  }
  const videos = Array.from(document.querySelectorAll('video'));
  result.videoCount = videos.length;
  const video = videos.find(v => v.offsetWidth > 0 && v.offsetHeight > 0) || videos[0];
  result.hasVideo = !!video;
  if (!video) {
    return result;
  }
  video.muted = true;
  video.volume = 0;
  video.playsInline = true;
  try {
    const playResult = video.play();
    if (playResult && playResult.then) {
      await playResult;
    }
    result.playResolved = true;
  } catch (error) {
    result.playError = String(error && (error.name || error.message) || error);
  }
  result.before = video.currentTime || 0;
  await sleep($($PlaybackSeconds * 1000));
  result.after = video.currentTime || 0;
  result.advanced = result.after > result.before + 0.5;
  result.paused = video.paused;
  result.readyState = video.readyState;
  result.networkState = video.networkState;
  result.videoWidth = video.videoWidth || 0;
  result.videoHeight = video.videoHeight || 0;
  return result;
})()
"@

    $lastError = $null
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        try {
            return Invoke-CdpExpression -Socket $Socket -NextId $NextId -Expression $probeScript -AwaitPromise $true
        } catch {
            $lastError = $_
            Start-Sleep -Seconds 2
        }
    }
    throw $lastError
}

$resolvedApp = Resolve-RepoPath -Path $AppExe
if (!(Test-Path -LiteralPath $resolvedApp -PathType Leaf)) {
    throw "CyberDeckBrowser.exe was not found: $resolvedApp"
}
if ((Split-Path -Leaf $resolvedApp) -ne "CyberDeckBrowser.exe") {
    throw "Video site tests must be run against CyberDeckBrowser.exe, not an external browser: $resolvedApp"
}
if (Test-TcpPortOpen -Port $RemoteDebuggingPort) {
    throw "Remote debugging port $RemoteDebuggingPort is already in use before launch. Close other browsers/apps or choose another port."
}

$previousPort = $env:CYBERDECK_CEF_REMOTE_DEBUGGING_PORT
$browserProcess = $null
$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$nextId = 1
$failures = @()

try {
    $env:CYBERDECK_CEF_REMOTE_DEBUGGING_PORT = [string]$RemoteDebuggingPort
    Write-Host "Launching CyberDeck with CEF remote debugging on 127.0.0.1:$RemoteDebuggingPort"
    $browserProcess = Start-Process -FilePath $resolvedApp -ArgumentList $Urls[0] -PassThru
    Start-Sleep -Milliseconds 500
    $browserProcess.Refresh()
    try {
        $launchedPath = $browserProcess.MainModule.FileName
    } catch {
        $launchedPath = $browserProcess.Path
    }
    Write-Host "Launched process:"
    Write-Host "  PID:  $($browserProcess.Id)"
    Write-Host "  Path: $launchedPath"

    Wait-JsonEndpoint -Url "http://127.0.0.1:$RemoteDebuggingPort/json/version" -TimeoutSeconds 20 | Out-Null
    $webSocketUrl = Get-PageWebSocketUrl -Port $RemoteDebuggingPort -TimeoutSeconds 25
    $null = $socket.ConnectAsync([Uri]$webSocketUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    Invoke-CdpCommand -Socket $socket -Id $nextId -Method "Runtime.enable" | Out-Null
    $nextId++
    Invoke-CdpCommand -Socket $socket -Id $nextId -Method "Page.enable" | Out-Null
    $nextId++

    foreach ($url in $Urls) {
        Write-Host "Testing video playback: $url"
        $result = Test-VideoUrl -Socket $socket -NextId ([ref]$nextId) -Url $url -PageTimeoutSeconds $PageTimeoutSeconds -PlaybackSeconds $PlaybackSeconds
        $result | Format-List
        if (!$result.hasVideo -or !$result.playResolved -or !$result.advanced) {
            $failures += $url
        }
    }
} finally {
    $env:CYBERDECK_CEF_REMOTE_DEBUGGING_PORT = $previousPort
    if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "done", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $socket.Dispose()

    if (!$KeepBrowserOpen -and $null -ne $browserProcess -and !$browserProcess.HasExited) {
        Stop-Process -Id $browserProcess.Id -Force
    }
}

if ($failures.Count -gt 0) {
    throw "Video playback failed for: $($failures -join ', ')"
}

Write-Host "All requested video site playback checks passed."
