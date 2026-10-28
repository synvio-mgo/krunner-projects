#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
if [ -f "$BUILD/install_manifest.txt" ]; then
    echo "==> Removing installed files (sudo)"
    sudo xargs rm -f < "$BUILD/install_manifest.txt"
    sudo rm -rf /usr/share/krunner-projects
fi
for d in "$HOME/.config/google-chrome" "$HOME/.config/chromium" "$HOME/.config/google-chrome-beta"; do
    rm -f "$d/NativeMessagingHosts/be.synvio.krunner_projects.json"
done
kquitapp6 krunner 2>/dev/null || true
(setsid krunner --replace >/dev/null 2>&1 &) || true
echo "Removed. Also remove the extension from chrome://extensions."
