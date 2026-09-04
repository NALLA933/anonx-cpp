#!/usr/bin/env bash
# ==============================================================================
# AnonX-CPP 1-Command Updater for Bare-Metal Systems
# ==============================================================================

set -euo pipefail

REPO="${ANONX_REPO:-yourusername/anonx-cpp}"
INSTALL_DIR="/opt/anonx"

echo "=== AnonX-CPP Self Updater ==="

ARCH=$(uname -m)
case "${ARCH}" in
    x86_64) TARGET="x86_64" ;;
    aarch64|arm64) TARGET="aarch64" ;;
    *) echo "Unsupported arch: ${ARCH}"; exit 1 ;;
esac

echo "--> Updating yt-dlp to latest version..."
sudo curl -sSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp
sudo chmod a+rx /usr/local/bin/yt-dlp

echo "--> Fetching latest AnonX binary for ${TARGET}..."
LATEST_TAG=$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/' || echo "")
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/anonx-bot-${TARGET}.tar.gz"

echo "--> Stopping anonx service..."
sudo systemctl stop anonx || true

curl -sSLf "${DOWNLOAD_URL}" -o /tmp/anonx-bot.tar.gz
sudo tar -xzf /tmp/anonx-bot.tar.gz -C "${INSTALL_DIR}"
rm -f /tmp/anonx-bot.tar.gz
sudo chmod +x "${INSTALL_DIR}/anonx_bot"

echo "--> Restarting anonx service..."
sudo systemctl start anonx
echo "=== AnonX updated and restarted successfully! ==="
