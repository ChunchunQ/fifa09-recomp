# FIFA 09 online revival server (EA Nation / "Aries" lobby protocol).
#
# FIFA 09 on Xbox 360 speaks EA's older text protocol (the one PS2/PSP EA
# games of 2004-2009 use): every packet is a 12-byte header - 4-char command,
# 4 bytes flags/status, 4-byte big-endian total length - followed by KEY=VALUE
# lines and a NUL. Replies follow the formats documented by the open-source
# ea-nation-server project (github.com/a-blondel/ea-nation-server).
#
# This version keeps a real lobby: several players, game rooms (gcre/gjoi/
# glea/gsta), quick match (gqwk) and game lists (gsea/gget).
#
#   powershell -ExecutionPolicy Bypass -File tools\aries-server.ps1
param(
  [int[]]$Ports = @(30460),
  [string]$PublicAddress = "",      # address handed to the game in @dir (default: the one it connected to)
  [switch]$Quiet                    # do not print every packet
)
$ErrorActionPreference = "Stop"
$log = Join-Path $PSScriptRoot "aries-capture.log"
"=== aries server started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $log -Append -Encoding utf8
$latin1 = [Text.Encoding]::GetEncoding(28591)

function Write-Log([string]$text, [string]$color = "Gray") {
  $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $text
  if (-not $Quiet -or $color -ne "Green") { Write-Host $line -ForegroundColor $color }
  $line | Out-File $log -Append -Encoding utf8
}

# ------------------------------------------------------------------ state

$script:nextUser = 1
$script:nextGame = 1
$script:nextRoom = 0
$script:sessions = New-Object Collections.ArrayList     # connected players
$script:games = [ordered]@{}                            # id -> game

function New-Session($client) {
  $s = [pscustomobject]@{
    Client = $client; Buf = (New-Object Collections.Generic.List[byte]); Id = $script:nextUser++
    Persona = ""; Xuid = ""; Maddr = ""; Ip = $client.Client.RemoteEndPoint.Address.ToString()
    Game = $null; Flags = "0"; UserParams = ""; Waiting = $false; WaitSysflags = "0"
  }
  [void]$script:sessions.Add($s)
  return $s
}

function Get-Fields([string]$body) {
  $map = [ordered]@{}
  $sep = if ($body.Contains("`n")) { "`n" } else { " " }
  foreach ($part in $body.Split($sep)) {
    $i = $part.IndexOf("=")
    if ($i -gt 0) { $map[$part.Substring(0, $i)] = $part.Substring($i + 1).TrimEnd("`r") }
  }
  return $map
}

# id: 4-char command, or 8 chars = command + 4-char error status (e.g. gjoifull)
function Send-Packet($s, [string]$id, $fields) {
  $text = ""
  if ($fields) { $text = (($fields.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`n") + "`0" }
  $body = $latin1.GetBytes($text)
  $total = 12 + $body.Length
  $packet = New-Object byte[] $total
  $head = $id.PadRight(8, [char]0).Substring(0, 8)
  $latin1.GetBytes($head).CopyTo($packet, 0)
  $packet[8] = [byte](($total -shr 24) -band 0xFF); $packet[9] = [byte](($total -shr 16) -band 0xFF)
  $packet[10] = [byte](($total -shr 8) -band 0xFF); $packet[11] = [byte]($total -band 0xFF)
  $body.CopyTo($packet, 12)
  try { $s.Client.GetStream().Write($packet, 0, $total) } catch { Write-Log "send to $($s.Persona) failed: $($_.Exception.Message)" "Red"; return }
  Write-Log ("  --> [{0}] {1} {2}" -f $s.Persona, $id, ($text -replace "`n", " " -replace "`0", "")) "Green"
}

# MA = the player's MADDR ("$<xuid>^<xnaddr>"). The game reads its own XUID
# from it ("self") and only creates the Xbox Live session as host when that
# matches the game's host; without it the host never opens a session.
function Get-Who($s) {
  $g = if ($s.Game) { "$($s.Game.Id)" } else { "0" }
  return [ordered]@{
    I = "$($s.Id)"; M = $s.Persona; N = $s.Persona; F = "U"; P = "80"; S = ",,,,,,,,,"; X = ""; G = $g; AT = ""; CL = "511"
    LV = "1049601"; MD = "0"; R = ""; US = ""; HW = "0"; RP = "0"; LO = "enUS"; CI = "0"; CT = "0"
    A = $s.Ip; LA = $s.Ip; MA = $s.Maddr; C = "4000,,7,1,1,,1,1,5553"; RI = "1"; RT = "$($script:sessions.Count)"; RG = "0"; RGC = "0"
  }
}

function Get-Room {
  return [ordered]@{ I = "1"; N = "LVL.1"; DN = "FIFA09"; D = ""; F = "A"; T = "$($script:sessions.Count)"; L = "50" }
}

function Get-GameInfo($g) {
  $info = [ordered]@{
    IDENT = "$($g.Id)"; NAME = $g.Name; HOST = $g.Host.Persona; PARAMS = $g.Params; PLATPARAMS = $g.PlatParams; ROOM = "1"
    CUSTFLAGS = "413082880"; SYSFLAGS = $g.Sysflags; COUNT = "$($g.Players.Count)"; PRIV = $g.Priv
    MINSIZE = "$($g.Min)"; MAXSIZE = "$($g.Max)"; NUMPART = "1"; SEED = "$($g.Seed)"; WHEN = $g.When
    AUTH = $(if (([int64]$g.Sysflags -band 262144) -ne 0) { "098f6bcd4621d373cade4e832627b4f6" } else { "" })
    # SESS = the host's Xbox Live session (XNADDR/XNKID/XNKEY text) and
    # PLATPARAMS = its nonce. Both come from the host's gset once its session
    # exists; the joiner needs them in +ses to join the session and connect.
    SESS = $g.Sess; EVID = "0"; EVGID = "0"
  }
  $i = 0
  foreach ($p in $g.Players) {
    $info["OPID$i"] = "$($p.Id)"; $info["OPPO$i"] = $p.Persona; $info["ADDR$i"] = $p.Ip; $info["LADDR$i"] = $p.Ip
    $info["MADDR$i"] = $p.Maddr; $info["OPPART$i"] = "0"; $info["OPPARAM$i"] = $p.UserParams; $info["OPFLAG$i"] = $p.Flags
    $info["PRES$i"] = "0"; $info["PARTSIZE$i"] = "$($g.Max)"; $info["PARTPARAMS$i"] = ""
    $i++
  }
  return $info
}

function Get-GameSummary($g) {
  return [ordered]@{ IDENT = "$($g.Id)"; NAME = $g.Name; PARAMS = $g.Params; SYSFLAGS = $g.Sysflags; COUNT = "$($g.Players.Count)"; MAXSIZE = "$($g.Max)" }
}

function New-Game($owner, $f) {
  $id = $script:nextGame++
  $name = if ($f["NAME"]) { $f["NAME"] } else { "$($owner.Persona)-$id" }
  $min = 2; $max = 2
  if ($f["MINSIZE"] -match '^\d+$') { $min = [int]$f["MINSIZE"] }
  if ($f["MAXSIZE"] -match '^\d+$') { $max = [int]$f["MAXSIZE"] }
  if ($max -lt 2 -or $max -gt 22) { $max = 2 }
  if ($min -lt 1 -or $min -gt $max) { $min = 2 }
  $g = [pscustomobject]@{
    Id = $id; Name = $name; Host = $owner; Players = (New-Object Collections.ArrayList)
    Params = "$($f["PARAMS"])"; Sysflags = $(if ($f["SYSFLAGS"]) { $f["SYSFLAGS"] } else { "0" })
    Pass = "$($f["PASS"])"; Priv = $(if ($f["PRIV"]) { $f["PRIV"] } else { "0" }); Min = $min; Max = $max
    Started = $false; Seed = (Get-Random -Maximum 100000); When = (Get-Date -Format "yyyy.M.d-HH:mm:ss")
    Sess = ""; PlatParams = "0"
  }
  [void]$g.Players.Add($owner)
  $owner.Game = $g; $owner.Flags = "1"
  $script:games["$id"] = $g
  Write-Log ("GAME {0} '{1}' created by {2} (max {3}, sysflags {4})" -f $id, $name, $owner.Persona, $max, $g.Sysflags) "Cyan"
  return $g
}

function Remove-FromGame($s) {
  $g = $s.Game
  if (-not $g) { return }
  $g.Players.Remove($s)
  $s.Game = $null; $s.Flags = "0"
  Write-Log ("GAME {0}: {1} left" -f $g.Id, $s.Persona) "Cyan"
  if ($g.Players.Count -eq 0 -or $g.Host -eq $s) {
    foreach ($p in @($g.Players)) { $p.Game = $null; Send-Packet $p "+mgm" $null }
    $script:games.Remove("$($g.Id)")
    Write-Log ("GAME {0} closed" -f $g.Id) "Cyan"
  } else {
    foreach ($p in $g.Players) { Send-Packet $p "+mgm" (Get-GameInfo $g) }
  }
}

function Join-Game($s, $g, [string]$replyId) {
  if ($g.Players.Count -ge $g.Max) { Send-Packet $s "$($replyId)full" $null; return $false }
  if ($g.Started) { Send-Packet $s "$($replyId)asta" $null; return $false }
  if ($s.Game) { Remove-FromGame $s }
  [void]$g.Players.Add($s)
  $s.Game = $g; $s.Flags = "0"
  Write-Log ("GAME {0}: {1} joined ({2}/{3})" -f $g.Id, $s.Persona, $g.Players.Count, $g.Max) "Cyan"
  Send-Packet $s $replyId (Get-GameInfo $g)
  Send-Packet $s "+who" (Get-Who $s)
  foreach ($p in $g.Players) { Send-Packet $p "+mgm" (Get-GameInfo $g) }
  return $true
}

function Start-Game($g) {
  $g.Started = $true
  Write-Log ("GAME {0} starting: {1}" -f $g.Id, (($g.Players | ForEach-Object { $_.Persona }) -join " vs ")) "Magenta"
  foreach ($p in $g.Players) { Send-Packet $p "+ses" (Get-GameInfo $g) }
}

# Quick match: join an open game with the same ranked flag, or pair two waiting players.
function Try-QuickMatch($s) {
  $ranked = ([int64]$s.WaitSysflags -band 262144) -ne 0
  foreach ($g in @($script:games.Values)) {
    $gRanked = ([int64]$g.Sysflags -band 262144) -ne 0
    if (-not $g.Started -and -not $g.Pass -and $g.Players.Count -lt $g.Max -and $gRanked -eq $ranked -and $g.Host -ne $s) {
      $s.Waiting = $false
      if (Join-Game $s $g "gjoi") { if ($g.Players.Count -ge $g.Max) { Start-Game $g } }
      return
    }
  }
  foreach ($o in @($script:sessions)) {
    if ($o -ne $s -and $o.Waiting) {
      $oRanked = ([int64]$o.WaitSysflags -band 262144) -ne 0
      if ($oRanked -ne $ranked) { continue }
      $o.Waiting = $false; $s.Waiting = $false
      $g = New-Game $o ([ordered]@{ NAME = "$($o.Persona)-quick"; MINSIZE = "2"; MAXSIZE = "2"; SYSFLAGS = $o.WaitSysflags; PARAMS = $o.WaitParams })
      [void]$g.Players.Add($s); $s.Game = $g
      Write-Log ("QUICK MATCH: {0} vs {1}" -f $o.Persona, $s.Persona) "Magenta"
      foreach ($p in $g.Players) { Send-Packet $p "+who" (Get-Who $p); Send-Packet $p "+mgm" (Get-GameInfo $g) }
      Start-Game $g
      return
    }
  }
  Write-Log ("QUICK MATCH: {0} waiting ({1})" -f $s.Persona, $(if ($ranked) { "ranked" } else { "unranked" })) "Cyan"
}

$qmsg = [ordered]@{
  IMGATE = "0"; QMSG0 = '"Do you want to play a game?"'; QMSG1 = "Yes"; QMSG2 = "No"
  QMSG3 = '"OK, let''s start"'; QMSG4 = '"Have fun!"'; QMSG5 = '"Good game!"'; QMSG6 = "Thanks!"
  QMSG7 = '"Catch you later"'; QMSG8 = '"See Ya!"'; SPM_EA = "0"; SPM_PART = "0"
}

# ---------------------------------------------------------------- handlers

function Handle-Packet($s, [string]$id, [string]$body) {
  $f = Get-Fields $body
  $addr = if ($PublicAddress) { $PublicAddress } else { $s.Client.Client.LocalEndPoint.Address.ToString() }
  switch -CaseSensitive ($id) {
    "~png" { return }
    "@tic" { Send-Packet $s "@tic" $null }
    "@dir" { Send-Packet $s "@dir" ([ordered]@{ ADDR = $addr; PORT = "$($s.Client.Client.LocalEndPoint.Port)" }) }
    "addr" { Send-Packet $s "addr" $null }
    "skey" { Send-Packet $s "skey" ([ordered]@{ SKEY = '$51ba8aee64ddfacae5baefa6bf61e009' }) }
    "news" {
      if ($f["NAME"] -eq "osdk_roster.cfg") {
        # No roster server: an empty config makes the game skip the squad
        # update check instead of trying to download a squad file.
        Send-Packet $s "news" $null; return
      }
      Send-Packet $s "news" ([ordered]@{
        BUDDY_SERVER = $addr; BUDDY_PORT = "10899"; CONTEXT = "buddy"
        TOS_URL = "http://$addr/tos/"; NEWS_URL = "http://$addr/news/"; ROSTER_URL = "http://$addr/roster"; ROSTER_VER = "1.0"
      })
    }
    "auth" {
      $name = if ($f["GTAG"]) { $f["GTAG"] } else { $f["NAME"] }
      $s.Xuid = "$($f["XUID"])"; $s.Maddr = "$($f["MADDR"])"
      # Same gamertag from two installs: keep them apart.
      if ($script:sessions | Where-Object { $_ -ne $s -and $_.Persona -eq $name }) { $name = "$name$($s.Id)" }
      $s.Persona = $name
      Write-Log ("LOGIN {0} (xuid {1}) from {2}" -f $name, $s.Xuid, $s.Ip) "Cyan"
      Send-Packet $s "auth" ([ordered]@{ NAME = $name; ADDR = $s.Ip; PERSONAS = $name; LOC = "enUS"; MAIL = "$name@fifa09.pc"; SPAM = "NN"; TOS = "1" })
    }
    "pers" {
      if ($f["MADDR"]) { $s.Maddr = $f["MADDR"] }
      Send-Packet $s "pers" ([ordered]@{ PERS = $s.Persona; LKEY = "3fcf27540c92935b0a66fd3b0000283c"; "EX-ticker" = ""; LOC = "enUS"; A = $s.Ip; LA = $s.Ip; IDLE = "100000" })
      Send-Packet $s "+who" (Get-Who $s)
    }
    "sele" {
      $out = [ordered]@{}
      foreach ($k in "INGAME", "GAMES", "MYGAME", "ROOMS", "USERS", "USERSETS", "MESGS", "MESGTYPES", "ASYNC", "STATS") {
        if ($f.Contains($k)) { $out[$k] = $f[$k] }
      }
      if ($out.Count -eq 0) { $out = [ordered]@{ MORE = "0"; SLOTS = "4"; STATS = "0" } } else { $out["SLOTS"] = "4" }
      Send-Packet $s "sele" $out
    }
    "onln" { Send-Packet $s "onln" $null }
    "user" { Send-Packet $s "user" ([ordered]@{ PERS = $s.Persona; LAST = "2008.9.6-12:00:00"; EXPR = "1"; STAT = "" }) }
    "uatr" { Send-Packet $s "uatr" $null }
    "rent" { Send-Packet $s "rent" $null }
    "qdef" { Send-Packet $s "qdef" $qmsg }
    "usld" { $m = [ordered]@{}; foreach ($k in $qmsg.Keys) { $m[$k] = $qmsg[$k] }; $m["UID"] = "$($s.Id)"; Send-Packet $s "usld" $m }
    "ussv" { Send-Packet $s "ussv" $null }
    "priv" { Send-Packet $s "priv" ([ordered]@{ PRIV = $(if ($f["MODE"] -eq "off") { "0" } else { "1" }) }) }
    "slst" { Send-Packet $s "slst" ([ordered]@{ COUNT = "1"; VIEW0 = 'Career,"My Career"' }) }
    "cate" {
      Send-Packet $s "cate" ([ordered]@{
        CC = "2"; IC = "2"; VC = "2"; SYMS = '"My Leaderboard","Top 100"'; SS = "23"; R = "0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1"
      })
    }
    "rcat" { Send-Packet $s "rcat" ([ordered]@{ CAT0 = "Default,Global,LVL,,,,,1" }); Send-Packet $s "+rom" (Get-Room) }
    "arom" { Send-Packet $s "arom" ([ordered]@{ COUNT = "1" }); Send-Packet $s "+rom" (Get-Room) }
    "move" {
      $ident = if ($f["IDENT"]) { $f["IDENT"] } else { "1" }
      if ($ident -eq "0") { Send-Packet $s "move" $null; return }
      Send-Packet $s "move" ([ordered]@{ IDENT = $ident; NAME = "LVL.1"; COUNT = "$($script:sessions.Count)"; FLAGS = "A"; LIMIT = "50"; DESC = "" })
      foreach ($o in $script:sessions) { if ($o.Persona) { Send-Packet $s "+usr" (Get-Who $o) } }
      Send-Packet $s "+who" (Get-Who $s)
    }

    "room" {
      # Private chat room for a game (NAME=$GAM.<game>): accept it.
      $script:nextRoom = 1 + [int]$script:nextRoom
      $rid = 100 + $script:nextRoom
      Send-Packet $s "room" ([ordered]@{ IDENT = "$rid"; NAME = $f["NAME"]; COUNT = "1"; FLAGS = "P"; LIMIT = "2"; DESC = "" })
    }

    # ---- games
    "gcre" {
      if ($s.Game) { Remove-FromGame $s }
      $s.UserParams = "$($f["USERPARAMS"])"
      $g = New-Game $s $f
      Send-Packet $s "gcre" (Get-GameInfo $g)
      Send-Packet $s "+who" (Get-Who $s)
      Send-Packet $s "+mgm" (Get-GameInfo $g)
      foreach ($o in $script:sessions) { if ($o -ne $s -and $o.Persona) { Send-Packet $o "+agm" (Get-GameInfo $g) } }
    }
    "gpsc" {
      if ($s.Game) { Remove-FromGame $s }
      $g = New-Game $s $f
      Send-Packet $s "gpsc" (Get-GameInfo $g)
      Send-Packet $s "+mgm" (Get-GameInfo $g)
    }
    "gjoi" {
      $s.UserParams = "$($f["USERPARAMS"])"
      $g = $null
      if ($f["IDENT"]) { $g = $script:games["$($f["IDENT"])"] }
      elseif ($f["NAME"]) { $g = @($script:games.Values | Where-Object { $_.Name -eq $f["NAME"] })[0] }
      if (-not $g) { Send-Packet $s "gjoiugam" $null; return }
      if ($g.Pass -and $f["PASS"] -ne $g.Pass) { Send-Packet $s "gjoipass" $null; return }
      [void](Join-Game $s $g "gjoi")
    }
    "glea" { Send-Packet $s "glea" $null; Remove-FromGame $s }
    "gdel" { Send-Packet $s "gdel" $null; if ($s.Game -and $s.Game.Host -eq $s) { Remove-FromGame $s } }
    "gsta" {
      Send-Packet $s "gsta" $null
      if ($s.Game) { Start-Game $s.Game }
    }
    "gset" {
      Send-Packet $s "gset" $null
      $g = $s.Game
      if ($g) {
        if ($f.Contains("PARAMS")) { $g.Params = $f["PARAMS"] }
        if ($f.Contains("SYSFLAGS")) { $g.Sysflags = $f["SYSFLAGS"] }
        if ($f.Contains("NAME")) { $g.Name = $f["NAME"] }
        $newSess = $false
        if ($f.Contains("SESS") -and $f["SESS"] -and $f["SESS"] -ne $g.Sess) {
          $g.Sess = $f["SESS"]; $newSess = $true
          Write-Log ("GAME {0}: host session published SESS={1}" -f $g.Id, $g.Sess) "Magenta"
        }
        if ($f.Contains("PLATPARAMS") -and $f["PLATPARAMS"]) { $g.PlatParams = $f["PLATPARAMS"] }
        foreach ($p in $g.Players) { Send-Packet $p "+mgm" (Get-GameInfo $g) }
        # The joiner reads SESS when it gets +ses; it got the first +ses before the
        # host's session existed, so start it again now that SESS is known.
        if ($newSess -and $g.Started) {
          foreach ($p in $g.Players) { if ($p -ne $g.Host) { Send-Packet $p "+ses" (Get-GameInfo $g) } }
        }
      }
    }
    "gsea" {
      $list = @($script:games.Values | Where-Object { -not $_.Started -and $_.Players.Count -lt $_.Max -and $_.Host -ne $s })
      $max = 20; if ($f["COUNT"] -match '^\d+$') { $max = [int]$f["COUNT"] }
      $list = @($list | Select-Object -First $max)
      Send-Packet $s "gsea" ([ordered]@{ COUNT = "$($list.Count)" })
      foreach ($g in $list) { Send-Packet $s "+gam" (Get-GameSummary $g) }
    }
    "gget" {
      $g = $null
      if ($f["IDENT"]) { $g = $script:games["$($f["IDENT"])"] }
      if ($g) { Send-Packet $s "gget" (Get-GameInfo $g) } else { Send-Packet $s "ggetugam" $null }
    }
    "gqwk" {
      Send-Packet $s "gqwk" $null
      switch ($f["MODE"]) {
        "2" {
          $s.Waiting = $true
          $s.WaitSysflags = $(if ($f["SYSFLAGS"]) { $f["SYSFLAGS"] } elseif ($f["SYSMASK"]) { $f["SYSMASK"] } else { "0" })
          $s | Add-Member -NotePropertyName WaitParams -NotePropertyValue "$($f["PARAMS"])" -Force
          Try-QuickMatch $s
        }
        "3" { $s.Waiting = $false; Write-Log ("QUICK MATCH: {0} cancelled" -f $s.Persona) "Cyan" }
      }
    }
    default {
      Write-Log "  (no handler yet for '$id' - sent an empty reply)" "Yellow"
      Send-Packet $s $id $null
    }
  }
}

function Close-Session($s, [string]$why) {
  Write-Log ("CLOSED {0} ({1})" -f $(if ($s.Persona) { $s.Persona } else { $s.Ip }), $why) "Red"
  try { Remove-FromGame $s } catch {}
  try { $s.Client.Close() } catch {}
  $script:sessions.Remove($s)
}

# ------------------------------------------------------------------- loop

$listeners = @()
foreach ($port in $Ports) {
  $l = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any, $port)
  $l.Start(); $listeners += $l
  Write-Log "listening on TCP $port"
}
$chunk = New-Object byte[] 65536
Write-Log "ready - start the game and open the online menu (Ctrl+C here to stop)" "Cyan"
try {
  while ($true) {
    foreach ($l in $listeners) {
      while ($l.Pending()) {
        $c = $l.AcceptTcpClient(); $c.NoDelay = $true
        $s = New-Session $c
        Write-Log ("CONNECT {0} -> port {1}" -f $c.Client.RemoteEndPoint, $l.LocalEndpoint.Port) "Cyan"
      }
    }
    foreach ($s in @($script:sessions)) {
      try {
        if ($s.Client.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead)) {
          $n = $s.Client.GetStream().Read($chunk, 0, $chunk.Length)
          if ($n -le 0) { Close-Session $s "closed by game"; continue }
          for ($k = 0; $k -lt $n; $k++) { $s.Buf.Add($chunk[$k]) }
        }
        while ($s.Buf.Count -ge 12) {
          $len = ([int]$s.Buf[8] -shl 24) -bor ([int]$s.Buf[9] -shl 16) -bor ([int]$s.Buf[10] -shl 8) -bor [int]$s.Buf[11]
          if ($len -lt 12 -or $len -gt 1MB) { Write-Log "bad packet length $len, dropping buffer" "Red"; $s.Buf.Clear(); break }
          if ($s.Buf.Count -lt $len) { break }
          $bytes = $s.Buf.GetRange(0, $len).ToArray(); $s.Buf.RemoveRange(0, $len)
          $id = $latin1.GetString($bytes, 0, 4)
          $flags = "{0:x2}{1:x2}{2:x2}{3:x2}" -f $bytes[4], $bytes[5], $bytes[6], $bytes[7]
          $body = $latin1.GetString($bytes, 12, $len - 12).TrimEnd([char]0)
          if ($id -ne "~png") {
            Write-Log ("<--  [{0}] {1} flags={2} {3}" -f $s.Persona, $id, $flags, ($body -replace "`n", " | "))
          }
          Handle-Packet $s $id $body
        }
      } catch {
        Close-Session $s $_.Exception.Message
      }
    }
    Start-Sleep -Milliseconds 5
  }
} finally {
  foreach ($l in $listeners) { $l.Stop() }
  Write-Log "stopped"
}
