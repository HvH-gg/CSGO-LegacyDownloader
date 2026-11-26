# Downloads an old CS:GO build (the game client, Windows) from Steam with DepotDownloader.
#
# It asks for the version, the folder and your Steam account name. The password and the Steam Guard code are asked
# by DepotDownloader itself, this script never sees them. Everything comes from Steam's own servers.
#
# Run: right click, "Run with PowerShell", or
#   powershell -ExecutionPolicy Bypass -File download-game.ps1 [-Version 2020|2019|2018|2016] [-Path <folder>] [-Username <steam name>]
param(
    [string]$Version,
    [string]$Path,
    [string]$Username,
    # only these files (a text file, one path per line, DepotDownloader's -filelist); for checking a download
    [string]$FileList
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$versionsFile = Join-Path $PSScriptRoot "versions.json"
if (-not (Test-Path -LiteralPath $versionsFile)) {
    Write-Host "versions.json is missing: it has to be in the same folder as this script." -ForegroundColor Red
    exit 1
}
$versions = (Get-Content -LiteralPath $versionsFile -Raw | ConvertFrom-Json).versions

# ---- which version
$chosen = $null
if ($Version) {
    $chosen = $versions | Where-Object { $_.id -eq $Version } | Select-Object -First 1
    if (-not $chosen) {
        Write-Host ("Unknown version '$Version'. Known: " + (($versions | ForEach-Object { $_.id }) -join ", ")) -ForegroundColor Red
        exit 1
    }
}
while (-not $chosen) {
    Write-Host ""
    Write-Host "Which version?"
    for ($i = 0; $i -lt $versions.Count; $i++) {
        Write-Host ("  {0}) {1}  ({2}, {3})" -f ($i + 1), $versions[$i].name, $versions[$i].patchVersion, $versions[$i].versionDate)
    }
    $answer = Read-Host "Number"
    $number = 0
    if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $versions.Count) {
        $chosen = $versions[$number - 1]
    }
}

# ---- where to
if (-not $Path) {
    $default = Join-Path $PSScriptRoot ("csgo-" + $chosen.id)
    $answer = Read-Host "Folder to download into [$default]"
    $Path = if ($answer) { $answer } else { $default }
}
New-Item -ItemType Directory -Force $Path | Out-Null
$Path = (Resolve-Path -LiteralPath $Path).Path

# ---- Steam account
if (-not $Username) {
    $Username = Read-Host "Steam account name (the game's Windows files are not available without a login)"
}
if (-not $Username) {
    Write-Host "No account name given." -ForegroundColor Red
    exit 1
}

# ---- DepotDownloader (https://github.com/SteamRE/DepotDownloader), into tools\ next to this script
$tools = Join-Path $PSScriptRoot "tools"
$depotDownloader = Join-Path $tools "DepotDownloader.exe"
if (-not (Test-Path -LiteralPath $depotDownloader)) {
    Write-Host "Getting DepotDownloader..."
    New-Item -ItemType Directory -Force $tools | Out-Null
    $zip = Join-Path $tools "DepotDownloader.zip"
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/SteamRE/DepotDownloader/releases/latest/download/DepotDownloader-windows-x64.zip" -OutFile $zip
    Expand-Archive -LiteralPath $zip -DestinationPath $tools -Force
    Remove-Item -LiteralPath $zip
}

# ---- download: every depot of the build into the same folder
Write-Host ""
Write-Host ("Downloading {0} ({1}) to {2}" -f $chosen.name, $chosen.patchVersion, $Path)
Write-Host "DepotDownloader asks for your password and Steam Guard code now. A stopped download continues when you run this again."
foreach ($depot in $chosen.client.depots) {
    $arguments = @("-app", $chosen.client.app, "-depot", $depot.depot, "-manifest", $depot.manifest,
        "-username", $Username, "-remember-password", "-dir", $Path)
    if ($FileList) { $arguments += @("-filelist", $FileList) }
    & $depotDownloader @arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("Depot {0} was not downloaded (DepotDownloader ended with {1})." -f $depot.depot, $LASTEXITCODE) -ForegroundColor Red
        exit 1
    }
}

# ---- the right build?
$steamInf = Join-Path $Path "csgo\steam.inf"
$patch = $null
if (Test-Path -LiteralPath $steamInf) {
    $line = Get-Content -LiteralPath $steamInf | Where-Object { $_ -like "PatchVersion=*" } | Select-Object -First 1
    if ($line) { $patch = $line.Split("=")[1].Trim() }
}
if ($patch -ne $chosen.patchVersion) {
    Write-Host ("The folder holds version '{0}', expected {1}." -f $patch, $chosen.patchVersion) -ForegroundColor Red
    exit 1
}

# ---- security patcher: a launcher that injects runtime fixes for this build's known
#      RCE bugs before the game runs. Found in a local build or next to this
#      script when present; otherwise downloaded from the latest GitHub release
#      (the CI workflow builds and publishes the two files there). One patcher
#      covers every build. Copied into the game folder either way.
$patcherDll = Join-Path $Path "csgo-patcher.dll"
$patcherExe = Join-Path $Path "csgo-patched-launcher.exe"
$patcherInstalled = $false
if (-not $FileList) {
    # 1) a prebuilt drop next to this script, or a local build output
    $patcherSources = @(
        (Join-Path $PSScriptRoot "patcher"),
        (Join-Path $PSScriptRoot "patcher\build\Release")
    )
    foreach ($src in $patcherSources) {
        $dll = Join-Path $src "csgo-patcher.dll"
        $exe = Join-Path $src "csgo-patched-launcher.exe"
        if ((Test-Path -LiteralPath $dll) -and (Test-Path -LiteralPath $exe)) {
            Copy-Item -LiteralPath $dll -Destination $patcherDll -Force
            Copy-Item -LiteralPath $exe -Destination $patcherExe -Force
            $patcherInstalled = $true
            break
        }
    }

    # 2) otherwise fetch the published build from GitHub releases. The CI workflow
    #    attaches csgo-patcher.dll + csgo-patched-launcher.exe to a release; find the
    #    newest release that carries both assets (not necessarily the very latest
    #    release, which could be a tool-only one) and download them from there.
    if (-not $patcherInstalled) {
        try {
            Write-Host "Getting the security patcher from GitHub releases..."
            $api = "https://api.github.com/repos/HvH-gg/CSGO-LegacyDownloader/releases?per_page=30"
            $headers = @{ "User-Agent" = "CSGO-LegacyDownloader"; "Accept" = "application/vnd.github+json" }
            $releases = Invoke-RestMethod -UseBasicParsing -Uri $api -Headers $headers
            $dllUrl = $null; $exeUrl = $null
            foreach ($rel in $releases) {
                if ($rel.draft) { continue }
                $d = $rel.assets | Where-Object { $_.name -eq "csgo-patcher.dll" } | Select-Object -First 1
                $e = $rel.assets | Where-Object { $_.name -eq "csgo-patched-launcher.exe" } | Select-Object -First 1
                if ($d -and $e) { $dllUrl = $d.browser_download_url; $exeUrl = $e.browser_download_url; break }
            }
            if (-not ($dllUrl -and $exeUrl)) { throw "no release carries the patcher assets yet" }
            Invoke-WebRequest -UseBasicParsing -Uri $dllUrl -OutFile $patcherDll
            Invoke-WebRequest -UseBasicParsing -Uri $exeUrl -OutFile $patcherExe
            $patcherInstalled = $true
        } catch {
            Remove-Item -LiteralPath $patcherDll, $patcherExe -ErrorAction SilentlyContinue
            Write-Host ("Could not download the security patcher ({0})." -f $_.Exception.Message) -ForegroundColor Yellow
        }
    }

    if (-not $patcherInstalled) {
        Write-Host "Security patcher not available; start.bat will run the game unpatched." -ForegroundColor Yellow
    }
}

# ---- how to start it: Steam has to run; -insecure keeps VAC out of a build Valve no longer serves.
#      With the patcher present, start.bat goes through the launcher (it injects the patcher
#      and adds -steam -insecure itself); otherwise it starts csgo.exe directly.
if (-not $FileList) {
    Set-Content -LiteralPath (Join-Path $Path "steam_appid.txt") -Value "730" -Encoding ascii
    $startLine = if ($patcherInstalled) {
        "start """" ""%~dp0csgo-patched-launcher.exe"" -novid %*"
    } else {
        "start """" ""%~dp0csgo.exe"" -steam -insecure -novid %*"
    }
    Set-Content -LiteralPath (Join-Path $Path "start.bat") -Encoding ascii -Value @(
        "@echo off",
        "rem Steam has to be running. -insecure: never start this build without it.",
        $startLine
    )
}

Write-Host ""
Write-Host ("Done: {0} ({1}) is in {2}" -f $chosen.name, $patch, $Path) -ForegroundColor Green
if (-not $FileList) {
    Write-Host "Start it with start.bat in that folder (Steam has to be running)."
    if ($patcherInstalled) { Write-Host "The security patcher is installed and runs automatically on start." -ForegroundColor Green }
}
