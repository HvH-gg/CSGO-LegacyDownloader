![Copyright ev0lve Digital](https://img.shields.io/badge/Copyright-ev0lve%20Digital-blue) ![GitHub License](https://img.shields.io/github/license/HvH-gg/CSGO-LegacyDownloader) ![Issues](https://img.shields.io/github/issues/HvH-gg/CSGO-LegacyDownloader) ![Downloads](https://img.shields.io/github/downloads/HvH-gg/CSGO-LegacyDownloader/total) ![Stars](https://img.shields.io/github/stars/HvH-gg/CSGO-LegacyDownloader)

# [HvH.gg](https://hvh.gg) CSGO Legacy Downloader

The CSGO Legacy Downloader lets you download and run the old CS:GO builds the legacy HvH scene still plays on. All files come straight from Steam, so no downloads from shady thirdparty sites are required.

# Features
- Download the game of an old CS:GO build (Windows)
- Install and run dedicated servers of an old CS:GO build (Linux)
- Several servers from one installation, each with its own config
- Shared settings for all your servers (`shared.conf`)
- Downloads are checked against the expected build and continue where they stopped

# Versions
| Version | Build | Build date |
|---|---|---|
| CS:GO 2020 | 1.37.6.4 | Aug 17 2020 |
| CS:GO 2019 | 1.37.3.1 | Dec 06 2019 |
| CS:GO 2018 | 1.36.2.9 | Mar 07 2018 |
| CS:GO 2016 | 1.35.6.2 | Dec 13 2016 |

# Requirements
**Game (Windows)**
- A Steam account: the game's Windows files are not available without a login
- Steam running when you start the game

**Server (Linux)**
- A 64 bit Linux (tested on Debian 12)
- `curl`, `unzip`, `jq`, `tmux`
- The 32 bit C and C++ runtime (`lib32gcc-s1`, `lib32stdc++6`)

The server script tells you what is missing and the `apt` command for it. It installs nothing itself.

# Game (Windows)
1. Download the latest release and extract it
2. Right click `download-game.ps1` and choose "Run with PowerShell", or run  
   `powershell -ExecutionPolicy Bypass -File download-game.ps1`
3. Choose the version, the folder and enter your Steam account name
4. Enter your password and Steam Guard code when DepotDownloader asks for them  
   4.1. The script never sees them
5. Start the game with `start.bat` in the folder you chose

`start.bat` starts the game with `-insecure`. Never start an old build without it.

To skip the questions: `download-game.ps1 -Version 2020 -Path D:\csgo-2020 -Username <steam name>`

# Server (Linux)
1. Download the latest release and extract it on your server
2. Run `./csgo-server.sh install` and choose the version and the folder  
   2.1. Or without questions: `./csgo-server.sh install 2018 /home/csgo/server-2018`
3. Fill in `conf/csgo-server.conf`: at least `PORT`, and `GSLT` for a public server
4. Run `./csgo-server.sh start`

The download is anonymous, you need no Steam account for the server.

# Commands
| Command | What it does |
|---|---|
| `./csgo-server.sh install [version] [folder]` | Downloads the server |
| `./csgo-server.sh start` | Starts the server in a tmux session |
| `./csgo-server.sh stop` | Stops the server |
| `./csgo-server.sh restart` | Stops and starts the server |
| `./csgo-server.sh console` | Opens the server console (leave it with `Ctrl+b`, then `d`) |
| `./csgo-server.sh status` | Shows version, folder, port and whether it runs |

# Config
Two files in the folder `conf` next to the script. `shared.conf` is read first, then the server's own file, so what the server's own file sets wins.

`conf/shared.conf`, for every server:
```bash
# The installation (written by "install").
VERSION="2018"
SERVER_DIR="/home/csgo/server-2018"

# Command line arguments every server gets, e.g. "-nobreakpad +sv_pure 0".
EXTRA_ARGS=""
```

`conf/csgo-server.conf`, the server's own settings. It is created for you to fill in when it is missing:
```bash
# Every server needs its own port.
PORT=27015

# Game server login token, one per server. Without one the server is LAN only.
GSLT=""

# Left out, these come from shared.conf, or are the defaults shown:
#MAP="de_mirage"
#MAX_PLAYERS=16
#TICKRATE=64
# 0/0 casual, 0/1 competitive, 1/2 deathmatch
#GAME_TYPE=0
#GAME_MODE=1

# Other ports of the server; left out, the game picks them.
#CLIENT_PORT=
#TV_PORT=

# Arguments of this server on top of the shared ones:
#EXTRA_ARGS="$EXTRA_ARGS +something"
```

Get your `GSLT` at [Steam Game Server Account Management](https://steamcommunity.com/dev/managegameservers) for app `730`.

The game's own config of a server (hostname, rcon password and so on) is `csgo/cfg/csgo-server.cfg` in the server folder. It is created empty at the first start and executed in place of `server.cfg`.

# Multiple servers
The name the script is called by is the server. For another server from the same installation, link or copy the script under another name:
```bash
ln -s csgo-server.sh csgo-server-2.sh
./csgo-server-2.sh start   # creates conf/csgo-server-2.conf to fill in, then start again
```

`csgo-server-2.sh` reads `conf/shared.conf` and `conf/csgo-server-2.conf`, runs in the tmux session `csgo-server-2` and executes `csgo/cfg/csgo-server-2.cfg`. The servers share the installation: plugins, maps and ban lists are the same for all of them. Each needs its own `PORT` and `GSLT`.

# Metamod:Source and SourceMod
Not installed by the scripts. An old server needs builds from its own time, newer ones carry game data for newer game builds. These are the builds that were current while each game build was live (by the date of the archive on the AlliedModders servers). Whether a later one still works was not tested.

| Version | Metamod:Source | SourceMod |
|---|---|---|
| CS:GO 2020 | 1.10.7-git971 or 1.11.0-git1141 | Branch 1.10, its builds of August 2020 are no longer on the download server |
| CS:GO 2019 | 1.10.7-git971 or 1.11.0-git1128 | Branch 1.10, its builds of December 2019 are no longer on the download server |
| CS:GO 2018 | 1.10.7-git961 or 1.11.0-git1103 | 1.8.0-git6041 |
| CS:GO 2016 | 1.10.7-git954 or 1.11.0-git1034 | Branch 1.8, its builds of December 2016 are no longer on the download server |

Downloads: [Metamod:Source](https://mms.alliedmods.net/mmsdrop/) and [SourceMod](https://sm.alliedmods.net/smdrop/).

# Good to know
- `install` sets aside the `libgcc_s.so.1` and `libstdc++.so.6` the server ships (renamed to `*.shipped`). With them the system's newer libraries do not load.
- `install` links `bin/steamclient.so` to `~/.steam/sdk32/`.
- An old server prints `MasterRequestRestart` (Steam asks it to update). That is expected.

# Adding a version
`versions.json` holds the depots and manifest ids of every version, for the game (app `730`: depot `731` common files, depot `732` Windows binaries) and for the server (app `740`: depot `731` again, depot `740` server binaries). Manifest ids are in the depot history on [SteamDB](https://steamdb.info/depot/731/manifests/) (login needed). `csgo/steam.inf` in depot `731` names the build (`PatchVersion`), both scripts check it after the download.

# Credits
- [HvH.gg](https://hvh.gg)
- [DepotDownloader](https://github.com/SteamRE/DepotDownloader)
- [SteamDB](https://steamdb.info/)
