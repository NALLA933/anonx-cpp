#!/usr/bin/env bash
set -e

echo "=== AnonX-CPP Production Container Engine ==="

# Auto-update yt-dlp on startup if enabled
if [ "${AUTO_UPDATE_YTDLP:-true}" = "true" ]; then
    echo "[Entrypoint] Checking for yt-dlp upstream updates..."
    if command -v yt-dlp &>/dev/null; then
        yt-dlp -U 2>&1 || echo "[Entrypoint] yt-dlp update check completed or skipped (non-critical)."
    fi
fi

# Ensure data directories exist
mkdir -p /app/data /app/sessions /app/downloads

echo "[Entrypoint] Starting AnonX Bot process ($@)..."
exec "$@"
