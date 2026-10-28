#!/usr/bin/env bash
# Build and install the runner, its config module, the Python bridge and the
# Chrome native messaging manifest.  The runner has to live in the system Qt
# plugin path, so the install step uses sudo.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr}"
BUILD="$HERE/build"

echo "==> Building (prefix $PREFIX)"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DKDE_INSTALL_USE_QT_SYS_PATHS=ON >/dev/null
cmake --build "$BUILD" -j"$(nproc)"

echo "==> Installing (sudo)"
sudo cmake --install "$BUILD"

DATADIR="$PREFIX/share/krunner-projects"
EXT_ID="$(cat "$DATADIR/extension_id.txt")"

echo "==> Registering the Chrome native messaging host"
for d in "$HOME/.config/google-chrome" "$HOME/.config/chromium" "$HOME/.config/google-chrome-beta"; do
    [ -d "$d" ] || continue
    mkdir -p "$d/NativeMessagingHosts"
    sed -e "s|@HOST_PATH@|$DATADIR/krunner_projects_host.py|" -e "s|@EXTENSION_ID@|$EXT_ID|" \
        "$DATADIR/be.synvio.krunner_projects.json.in" > "$d/NativeMessagingHosts/be.synvio.krunner_projects.json"
    echo "    $d/NativeMessagingHosts/be.synvio.krunner_projects.json"
done

echo "==> Restarting KRunner"
kquitapp6 krunner 2>/dev/null || true
sleep 1
(setsid krunner --replace >/dev/null 2>&1 &) || true

cat <<EOF

Done.

One manual step remains, once: load the Chrome extension.
  1. Open chrome://extensions and switch on "Developer mode" (top right).
  2. "Load unpacked" -> choose:  $DATADIR/extension
  3. Check that its ID is  $EXT_ID
     (it is fixed by the key in manifest.json, so it should be).

Then configure projects in System Settings > Search > KRunner > "Projects & Chrome groups" (configure icon),
or try KRunner right away:   cg <group name>
EOF
