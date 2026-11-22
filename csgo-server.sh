#!/usr/bin/env bash
# Installs and runs dedicated servers of an old CS:GO build on Linux (64 bit, the server itself is 32 bit).
#
#   ./csgo-server.sh install [2020|2019|2018|2016] [folder]   download the server
#   ./csgo-server.sh start | stop | restart              the server runs in a tmux session
#   ./csgo-server.sh console                             open the server console (leave it with Ctrl+b, then d)
#   ./csgo-server.sh status                              installed version, folder, running or not
#
# The files come from Steam's own servers with DepotDownloader (anonymous login, no Steam account needed).
#
# Settings are in the folder conf/ next to the script: shared.conf (for every server), then the server's own file,
# named like the script: csgo-server.sh reads conf/csgo-server.conf. For a second server from the same installation,
# link or copy the script under another name: "ln -s csgo-server.sh csgo-server-2.sh" reads conf/csgo-server-2.conf.
set -euo pipefail

# The name it was called by is the server: csgo-server, csgo-server-2, ...
INVOKED="${BASH_SOURCE[0]}"
INSTANCE="$(basename "$INVOKED" .sh)"
BASE_DIR="$(cd "$(dirname "$INVOKED")" && pwd)"
CONFIG_DIR="$BASE_DIR/conf"
# versions.json and the tools are next to the script itself, also when it was called through a link
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$INVOKED")")" && pwd)"
VERSIONS_FILE="$SCRIPT_DIR/versions.json"
TOOLS_DIR="$SCRIPT_DIR/tools"
DEPOT_DOWNLOADER="$TOOLS_DIR/DepotDownloader"
SHARED_CONFIG="$CONFIG_DIR/shared.conf"
CONFIG_FILE="${CSGO_SERVER_CONF:-$CONFIG_DIR/$INSTANCE.conf}"
SESSION="$INSTANCE"

say() { printf '%s\n' "$*"; }
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '2,13p' "$(readlink -f "$INVOKED")" | sed 's/^# \{0,1\}//'
}

# ---- what the script and the server need; nothing is installed for you
check_commands() {
    local missing=()
    for command in "$@"; do
        command -v "$command" >/dev/null 2>&1 || missing+=("$command")
    done
    if ((${#missing[@]} > 0)); then
        say "Missing: ${missing[*]}"
        say "On Debian or Ubuntu: sudo apt install ${missing[*]}"
        exit 1
    fi
}

# The server is a 32 bit program: it needs the 32 bit C and C++ runtime.
check_32bit_libraries() {
    local missing=0
    for library in libgcc_s.so.1 libstdc++.so.6; do
        local found=0
        for folder in /lib32 /usr/lib32 /lib/i386-linux-gnu /usr/lib/i386-linux-gnu; do
            [[ -e "$folder/$library" ]] && found=1
        done
        if ((!found)); then
            say "Missing 32 bit library: $library"
            missing=1
        fi
    done
    if ((missing)); then
        say "On Debian or Ubuntu: sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install lib32gcc-s1 lib32stdc++6"
        return 1
    fi
}

# ---- settings
write_shared_config() {
    local version="$1" folder="$2"
    mkdir -p "$CONFIG_DIR"
    if [[ -f "$SHARED_CONFIG" ]]; then
        # keep the settings; the installed version and folder are what changed
        if grep -q '^VERSION=' "$SHARED_CONFIG"; then
            sed -i "s|^VERSION=.*|VERSION=\"$version\"|" "$SHARED_CONFIG"
        else
            printf 'VERSION="%s"\n' "$version" >>"$SHARED_CONFIG"
        fi
        if grep -q '^SERVER_DIR=' "$SHARED_CONFIG"; then
            sed -i "s|^SERVER_DIR=.*|SERVER_DIR=\"$folder\"|" "$SHARED_CONFIG"
        else
            printf 'SERVER_DIR="%s"\n' "$folder" >>"$SHARED_CONFIG"
        fi
        return
    fi
    cat >"$SHARED_CONFIG" <<EOF
# Settings for every server. A server's own file (csgo-server.conf, csgo-server-2.conf, ...) is read after this
# one: what it sets wins.

# The installation (written by "install").
VERSION="$version"
SERVER_DIR="$folder"

# Command line arguments every server gets, e.g. "-nobreakpad +sv_pure 0".
EXTRA_ARGS=""

# Any setting of a server's own file can stand here instead, for all servers:
#MAP="de_mirage"
#MAX_PLAYERS=16
#TICKRATE=64
#GAME_TYPE=0
#GAME_MODE=1
EOF
}

# A server's own file: everything that has to differ between servers is to be filled in.
write_instance_config() {
    mkdir -p "$(dirname "$CONFIG_FILE")"
    cat >"$CONFIG_FILE" <<EOF
# Settings of the server "$INSTANCE". Read after shared.conf: what is set here wins.
# Fill in PORT and GSLT, then: ./$INSTANCE.sh start

# Every server needs its own port.
PORT=

# Game server login token (https://steamcommunity.com/dev/managegameservers, app 730), one per server. Without one
# the server is LAN only.
GSLT=""

# Left out, these come from shared.conf, or are the defaults shown:
#MAP="de_mirage"
#MAX_PLAYERS=16
#TICKRATE=64
# 0/0 casual, 0/1 competitive, 1/2 deathmatch
#GAME_TYPE=0
#GAME_MODE=1

# Other ports of the server; left out, the game picks them (27005 and 27020, or the next free ones).
#CLIENT_PORT=
#TV_PORT=

# Arguments of this server on top of the shared ones:
#EXTRA_ARGS="\$EXTRA_ARGS +something"
EOF
}

load_config() {
    [[ -f "$SHARED_CONFIG" ]] || fail "no $SHARED_CONFIG yet, run: $INVOKED install"
    # shellcheck disable=SC1090
    source "$SHARED_CONFIG"
    if [[ ! -f "$CONFIG_FILE" ]]; then
        write_instance_config
        say "The server \"$INSTANCE\" had no settings yet: $CONFIG_FILE was created."
        say "Fill it in (at least PORT), then run this again."
        exit 1
    fi
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
    [[ -n "${VERSION:-}" && -n "${SERVER_DIR:-}" ]] || fail "neither $SHARED_CONFIG nor $CONFIG_FILE names VERSION and SERVER_DIR"
}

get_depot_downloader() {
    [[ -x "$DEPOT_DOWNLOADER" ]] && return
    say "Getting DepotDownloader..."
    mkdir -p "$TOOLS_DIR"
    curl -fsSL -o "$TOOLS_DIR/DepotDownloader.zip" \
        "https://github.com/SteamRE/DepotDownloader/releases/latest/download/DepotDownloader-linux-x64.zip"
    unzip -o -q "$TOOLS_DIR/DepotDownloader.zip" -d "$TOOLS_DIR"
    rm "$TOOLS_DIR/DepotDownloader.zip"
    chmod +x "$DEPOT_DOWNLOADER"
}

# ---- install
install_server() {
    local version="${1:-}" folder="${2:-}"
    check_commands curl unzip jq tmux
    [[ -f "$VERSIONS_FILE" ]] || fail "versions.json is missing: it has to be next to the script"

    local known
    known="$(jq -r '.versions[].id' "$VERSIONS_FILE" | tr '\n' ' ')"
    if [[ -z "$version" ]]; then
        say "Which version?"
        jq -r '.versions[] | "  \(.id)  \(.name) (\(.patchVersion), \(.versionDate))"' "$VERSIONS_FILE"
        read -r -p "Version: " version
    fi
    jq -e --arg id "$version" '.versions[] | select(.id == $id)' "$VERSIONS_FILE" >/dev/null \
        || fail "unknown version '$version' (known: $known)"

    if [[ -z "$folder" ]]; then
        local default="$BASE_DIR/server-$version"
        read -r -p "Folder to install into [$default]: " folder
        folder="${folder:-$default}"
    fi
    mkdir -p "$folder"
    folder="$(cd "$folder" && pwd)"

    get_depot_downloader

    local app patch
    app="$(jq -r --arg id "$version" '.versions[] | select(.id == $id) | .server.app' "$VERSIONS_FILE")"
    patch="$(jq -r --arg id "$version" '.versions[] | select(.id == $id) | .patchVersion' "$VERSIONS_FILE")"
    say "Downloading the $version server ($patch) to $folder. A stopped download continues when you run this again."
    # CSGO_SERVER_FILELIST: only these files (DepotDownloader's -filelist), for checking a download
    local extra=()
    [[ -n "${CSGO_SERVER_FILELIST:-}" ]] && extra=(-filelist "$CSGO_SERVER_FILELIST")
    while read -r depot manifest; do
        "$DEPOT_DOWNLOADER" -app "$app" -depot "$depot" -manifest "$manifest" -dir "$folder" \
            ${extra[@]+"${extra[@]}"} </dev/null || fail "depot $depot was not downloaded"
    done < <(jq -r --arg id "$version" '.versions[] | select(.id == $id) | .server.depots[] | "\(.depot) \(.manifest)"' \
        "$VERSIONS_FILE")

    local got
    got="$(grep -m1 '^PatchVersion=' "$folder/csgo/steam.inf" 2>/dev/null | cut -d= -f2 | tr -d '\r' || true)"
    [[ "$got" == "$patch" ]] || fail "the folder holds version '$got', expected $patch"

    if [[ -z "${CSGO_SERVER_FILELIST:-}" ]]; then
        prepare_server "$folder"
    fi
    write_shared_config "$version" "$folder"

    say ""
    say "Installed: $version ($patch) in $folder"
    say "Shared settings: $SHARED_CONFIG"
    if [[ ! -f "$CONFIG_FILE" ]]; then
        write_instance_config
        say "Settings of this server: $CONFIG_FILE was created. Fill it in (at least PORT) before the first start."
    fi
    check_32bit_libraries || say "Install the libraries above before the first start."
    say "Start it with: $INVOKED start"
}

# What an old server needs on a current system.
prepare_server() {
    local folder="$1"
    chmod +x "$folder/srcds_run" "$folder/srcds_linux" 2>/dev/null || true
    # The server ships its own, old libgcc and libstdc++; with them the system's newer libraries fail to load.
    # They are set aside, not deleted.
    for library in libgcc_s.so.1 libstdc++.so.6; do
        if [[ -f "$folder/bin/$library" ]]; then
            mv "$folder/bin/$library" "$folder/bin/$library.shipped"
        fi
    done
    # Steam's client library where the server looks for it.
    mkdir -p "$HOME/.steam/sdk32"
    ln -sf "$folder/bin/steamclient.so" "$HOME/.steam/sdk32/steamclient.so"
}

# ---- run
is_running() { tmux has-session -t "=$SESSION" 2>/dev/null; }

start_server() {
    check_commands tmux
    load_config
    is_running && fail "already running (tmux session $SESSION)"
    [[ -n "${PORT:-}" ]] || fail "PORT is not set in $CONFIG_FILE"
    [[ -x "$SERVER_DIR/srcds_run" ]] || fail "no server in $SERVER_DIR, run: $INVOKED install"
    check_32bit_libraries || exit 1

    # The server's own config in the game, in place of server.cfg: csgo/cfg/<name>.cfg
    local game_config="$SERVER_DIR/csgo/cfg/$INSTANCE.cfg"
    if [[ ! -f "$game_config" ]]; then
        mkdir -p "$(dirname "$game_config")"
        printf '// Config of the server "%s", executed in place of server.cfg (hostname, rcon_password, ...).\n' \
            "$INSTANCE" >"$game_config"
        say "Created $game_config for this server's game settings."
    fi

    local arguments=(-game csgo -console -usercon -port "$PORT" -tickrate "${TICKRATE:-64}"
        -maxplayers_override "${MAX_PLAYERS:-16}" +game_type "${GAME_TYPE:-0}" +game_mode "${GAME_MODE:-1}"
        +servercfgfile "$INSTANCE.cfg" +map "${MAP:-de_mirage}")
    [[ -n "${CLIENT_PORT:-}" ]] && arguments+=(+clientport "$CLIENT_PORT")
    [[ -n "${TV_PORT:-}" ]] && arguments+=(+tv_port "$TV_PORT")
    [[ -n "${GSLT:-}" ]] && arguments+=(+sv_setsteamaccount "$GSLT")
    # shellcheck disable=SC2206
    [[ -n "${EXTRA_ARGS:-}" ]] && arguments+=($EXTRA_ARGS)

    tmux new-session -d -s "$SESSION" -c "$SERVER_DIR" "./srcds_run ${arguments[*]}"
    sleep 2
    is_running || fail "the server ended right away, start it by hand to see why: cd $SERVER_DIR && ./srcds_run ${arguments[*]}"
    say "Started \"$INSTANCE\" ($VERSION) on port $PORT (console: $INVOKED console)"
}

stop_server() {
    check_commands tmux
    load_config
    is_running || { say "Not running."; return; }
    tmux send-keys -t "=$SESSION:" "quit" Enter
    for _ in $(seq 1 10); do
        is_running || break
        sleep 1
    done
    is_running && tmux kill-session -t "=$SESSION"
    say "Stopped."
}

console() {
    check_commands tmux
    load_config
    is_running || fail "not running"
    say "Leave the console with Ctrl+b, then d (Ctrl+c would stop the server)."
    sleep 1
    tmux attach -t "=$SESSION"
}

status() {
    load_config
    say "Server:  $INSTANCE"
    say "Version: $VERSION"
    say "Folder:  $SERVER_DIR"
    say "Port:    ${PORT:-not set}"
    if command -v tmux >/dev/null 2>&1 && is_running; then say "Running: yes (tmux session $SESSION)"; else say "Running: no"; fi
}

case "${1:-}" in
    install) install_server "${2:-}" "${3:-}" ;;
    start) start_server ;;
    stop) stop_server ;;
    restart) stop_server; start_server ;;
    console) console ;;
    status) status ;;
    *) usage; exit 1 ;;
esac
