#!/usr/bin/env bash
# ==============================================================================
# AnonX-CPP 1-Command Bare-Metal Installer
# Designed for 1 vCPU / 1 GB RAM VPS (Zero Compilation, Zero OOM Risk)
# ==============================================================================

set -euo pipefail

REPO="${ANONX_REPO:-yourusername/anonx-cpp}"
INSTALL_DIR="/opt/anonx"

echo "=========================================================="
echo "          AnonX-CPP High-Performance Installer            "
echo "=========================================================="

# 1. Check Root / Sudo
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo &>/dev/null; then
        SUDO="sudo"
    else
        echo "Error: This script requires root privileges or sudo."
        exit 1
    fi
fi

# 2. Architecture Detection
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64)
        TARGET="x86_64"
        ;;
    aarch64|arm64)
        TARGET="aarch64"
        ;;
    *)
        echo "Error: Unsupported CPU architecture: ${ARCH}. Only x86_64 and aarch64 are supported."
        exit 1
        ;;
esac
echo "[1/5] Detected architecture: ${TARGET}"

# 3. Install Runtime Dependencies
echo "[2/5] Checking and installing runtime dependencies..."
if command -v apt-get &>/dev/null; then
    $SUDO apt-get update -qq
    $SUDO apt-get install -y -qq curl tar ffmpeg python3 ca-certificates &>/dev/null
elif command -v apk &>/dev/null; then
    $SUDO apk add --no-cache curl tar ffmpeg python3 ca-certificates
elif command -v dnf &>/dev/null; then
    $SUDO dnf install -y -q curl tar ffmpeg python3 ca-certificates
fi

# 4. Install / Update Standalone yt-dlp
echo "[3/5] Installing latest official standalone yt-dlp binary..."
$SUDO curl -sSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp
$SUDO chmod a+rx /usr/local/bin/yt-dlp

# 5. Fetch Latest Release Archive
echo "[4/5] Downloading pre-compiled native binary for ${TARGET}..."
$SUDO mkdir -p "${INSTALL_DIR}" "${INSTALL_DIR}/data" "${INSTALL_DIR}/sessions" "${INSTALL_DIR}/downloads"

LATEST_TAG=$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/' || echo "")

if [ -z "${LATEST_TAG}" ]; then
    echo "Warning: Could not fetch latest release tag automatically from GitHub API (rate limit or private repo)."
    echo "Falling back to 'latest' release assets..."
    DOWNLOAD_URL="https://github.com/${REPO}/releases/latest/download/anonx-bot-${TARGET}.tar.gz"
else
    echo "Found latest version: ${LATEST_TAG}"
    DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/anonx-bot-${TARGET}.tar.gz"
fi

if curl -sSLf "${DOWNLOAD_URL}" -o /tmp/anonx-bot.tar.gz; then
    $SUDO tar -xzf /tmp/anonx-bot.tar.gz -C "${INSTALL_DIR}"
    rm -f /tmp/anonx-bot.tar.gz
    $SUDO chmod +x "${INSTALL_DIR}/anonx_bot"
else
    echo "Notice: Prebuilt binary archive not found at ${DOWNLOAD_URL}."
    echo "If you are running from source repo, building local binary..."
    if [ -f "./build/anonx_bot" ]; then
        $SUDO cp ./build/anonx_bot "${INSTALL_DIR}/anonx_bot"
        $SUDO chmod +x "${INSTALL_DIR}/anonx_bot"
    fi
fi

# 6. Create systemd daemon
echo "[5/5] Configuring systemd service..."
SERVICE_FILE="/etc/systemd/system/anonx.service"
CURRENT_USER="${SUDO_USER:-$(whoami)}"

$SUDO tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=AnonX C++ Telegram Music Bot Daemon
After=network.target

[Service]
Type=simple
User=${CURRENT_USER}
WorkingDirectory=${INSTALL_DIR}
ExecStart=${INSTALL_DIR}/anonx_bot
Restart=always
RestartSec=5
LimitNOFILE=65535
Environment=PATH=/usr/local/bin:/usr/bin:/bin

[Install]
WantedBy=multi-user.target
EOF

$SUDO systemctl daemon-reload

echo ""
echo "=========================================================="
echo "          AnonX-CPP Installation Completed!               "
echo "=========================================================="
echo "Next Steps:"
echo "1. Configure your bot settings in:"
echo "   ${INSTALL_DIR}/config.json"
echo ""
echo "2. Enable and start the background service:"
echo "   sudo systemctl enable --now anonx"
echo ""
echo "3. Monitor live logs:"
echo "   sudo journalctl -u anonx -f"
echo "=========================================================="
