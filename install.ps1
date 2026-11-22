# Fast track for the game: gets download-game.ps1 and versions.json and starts the downloader.
#
#   irm https://legacy.hvh.gg/game | iex
#
# The scripts go into .\CSGO-LegacyDownloader in the folder you are in ($env:CSGO_LEGACY_DIR names another one).
& {
    $ErrorActionPreference = "Stop"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    $source = if ($env:CSGO_LEGACY_SOURCE) { $env:CSGO_LEGACY_SOURCE } else { "https://raw.githubusercontent.com/HvH-gg/CSGO-LegacyDownloader/master" }
    $target = if ($env:CSGO_LEGACY_DIR) { $env:CSGO_LEGACY_DIR } else { Join-Path (Get-Location).Path "CSGO-LegacyDownloader" }

    New-Item -ItemType Directory -Force $target | Out-Null
    Write-Host "Getting the scripts into $target"
    foreach ($file in @("download-game.ps1", "versions.json")) {
        Invoke-WebRequest -UseBasicParsing -Uri "$source/$file" -OutFile (Join-Path $target $file)
    }

    # (its own process: scripts from the internet are not allowed to run in every PowerShell)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $target "download-game.ps1")
}
