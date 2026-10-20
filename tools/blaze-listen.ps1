# Step 1 of the FIFA 09 online revival: a listening post, not a server yet.
# Accepts TCP connections on the ports EA's Blaze clients are known to use and
# writes everything the game sends to tools\blaze-capture.log (hex + text).
# It never answers, so the game will eventually say the server is unavailable;
# that is expected. We only want the game's first messages.
#
#   powershell -ExecutionPolicy Bypass -File tools\blaze-listen.ps1
#
# Windows may ask to allow network access the first time: allow it.
param([int[]]$Ports = @(30460, 42127, 42100, 10041, 3659, 10744, 9946, 80, 443, 8080))
$ErrorActionPreference = "Stop"
$log = Join-Path $PSScriptRoot "blaze-capture.log"
"=== capture started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $log -Append -Encoding utf8

function Write-Log([string]$text) {
  $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $text
  Write-Host $line
  $line | Out-File $log -Append -Encoding utf8
}

function Format-Dump([byte[]]$data, [int]$count) {
  $sb = New-Object Text.StringBuilder
  for ($i = 0; $i -lt $count; $i += 16) {
    $n = [Math]::Min(16, $count - $i)
    $hex = ($data[$i..($i + $n - 1)] | ForEach-Object { $_.ToString("x2") }) -join " "
    $txt = -join ($data[$i..($i + $n - 1)] | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { "." } })
    [void]$sb.AppendLine(("    {0:x4}  {1,-47}  {2}" -f $i, $hex, $txt))
  }
  return $sb.ToString().TrimEnd()
}

$listeners = @()
foreach ($port in $Ports) {
  try {
    $l = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any, $port)
    $l.Start()
    $listeners += $l
    Write-Log "listening on TCP $port"
  } catch {
    Write-Log "cannot listen on TCP $port ($($_.Exception.Message))"
  }
}

$clients = New-Object Collections.ArrayList
$buf = New-Object byte[] 65536
Write-Log "ready - start the game and open the online menu (Ctrl+C here to stop)"
try {
  while ($true) {
    foreach ($l in $listeners) {
      while ($l.Pending()) {
        $c = $l.AcceptTcpClient()
        [void]$clients.Add($c)
        Write-Log ("CONNECT {0} -> port {1}" -f $c.Client.RemoteEndPoint, $l.LocalEndpoint.Port)
      }
    }
    foreach ($c in @($clients)) {
      try {
        $port = $c.Client.LocalEndPoint.Port
        if ($c.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead)) {
          $n = $c.GetStream().Read($buf, 0, $buf.Length)
          if ($n -le 0) {
            Write-Log "CLOSED by game (port $port)"
            $c.Close(); $clients.Remove($c)
          } else {
            Write-Log "RECV port $port, $n bytes"
            Format-Dump $buf $n | Out-File $log -Append -Encoding utf8
            Write-Host (Format-Dump $buf ([Math]::Min($n, 64)))
          }
        }
      } catch {
        Write-Log "connection error: $($_.Exception.Message)"
        $c.Close(); $clients.Remove($c)
      }
    }
    Start-Sleep -Milliseconds 15
  }
} finally {
  foreach ($l in $listeners) { $l.Stop() }
  Write-Log "stopped"
}
