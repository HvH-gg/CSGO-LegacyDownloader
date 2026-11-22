#!/usr/bin/env bash
# Fast track for the server: gets csgo-server.sh and versions.json and starts the install.
#
#   curl -fsSL https://legacy.hvh.gg/server | bash
#   curl -fsSL https://legacy.hvh.gg/server | bash -s -- 2018 /home/csgo/server-2018    (version and folder, no questions)
#
# The scripts go into ./csgo-legacy (CSGO_LEGACY_DIR names another folder). Run again to update them: settings in
# conf/ are kept.
set -euo pipefail

SOURCE="${CSGO_LEGACY_SOURCE:-https://raw.githubusercontent.com/HvH-gg/CSGO-LegacyDownloader/master}"
TARGET="${CSGO_LEGACY_DIR:-$PWD/csgo-legacy}"

command -v curl >/dev/null 2>&1 || { echo "Error: curl is missing (sudo apt install curl)" >&2; exit 1; }

mkdir -p "$TARGET"
TARGET="$(cd "$TARGET" && pwd)"
echo "Getting the scripts into $TARGET"
for file in csgo-server.sh versions.json; do
    curl -fsSL -o "$TARGET/$file.new" "$SOURCE/$file" || { rm -f "$TARGET/$file.new"; echo "Error: $SOURCE/$file could not be downloaded" >&2; exit 1; }
    mv "$TARGET/$file.new" "$TARGET/$file"
done
chmod +x "$TARGET/csgo-server.sh"

cd "$TARGET"
if [[ $# -gt 0 ]]; then
    # version (and folder) given: nothing to ask
    exec ./csgo-server.sh install "$@"
elif { true </dev/tty; } 2>/dev/null; then
    # piped into bash, the keyboard is not the standard input: the questions read from the terminal
    exec ./csgo-server.sh install </dev/tty
else
    echo "No terminal to ask on. Continue with: cd $TARGET && ./csgo-server.sh install"
fi
