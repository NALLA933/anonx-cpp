#!/usr/bin/env bash

set -e

BOLD='\033[1m'
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

clear 2>/dev/null || true
echo -e "${CYAN}${BOLD}"
echo "======================================================================"
echo "          SenpaiMusic (C++ Port) — 1-Click Installer & Runner          "
echo "======================================================================"
echo -e "${NC}"

log_info() {
    echo -e "${BLUE}${BOLD}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}${BOLD}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}${BOLD}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}${BOLD}[ERROR]${NC} $1"
}

log_info "Checking system dependencies..."

REQUIRED_PACKAGES=(cmake g++ make ffmpeg libsqlite3-dev nlohmann-json3-dev libssl-dev unzip libasound2 libpulse0)
MISSING_PACKAGES=()

if command -v dpkg-query >/dev/null 2>&1; then
    for pkg in "${REQUIRED_PACKAGES[@]}"; do
        if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done
elif command -v apt-get >/dev/null 2>&1; then

    MISSING_PACKAGES=("${REQUIRED_PACKAGES[@]}")
fi

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    log_warn "Missing required packages: ${MISSING_PACKAGES[*]}"
    log_info "Installing missing dependencies via apt-get (sudo may prompt for password)..."

    SUDO_CMD=""
    if [ "$(id -u)" -ne 0 ]; then
        if command -v sudo >/dev/null 2>&1; then
            SUDO_CMD="sudo"
        else
            log_error "'sudo' command not found. Please run as root or install sudo."
            exit 1
        fi
    fi

    $SUDO_CMD apt-get update -qq
    $SUDO_CMD apt-get install -y -qq "${MISSING_PACKAGES[@]}"
    log_success "All system dependencies installed successfully."
else
    log_success "All required dependencies are installed."
fi

log_info "Checking Telegram TDLib library (libtdjson.so)..."
TDLIB_FOUND=false
for dir in "/usr/local/lib" "/usr/lib" "/usr/lib/x86_64-linux-gnu" "$PWD/lib"; do
    if [ -f "$dir/libtdjson.so" ]; then
        TDLIB_FOUND=true
        log_success "TDLib library found in $dir/libtdjson.so"
        break
    fi
done

if [ "$TDLIB_FOUND" = false ]; then
    log_info "libtdjson.so not found. Downloading prebuilt official TDLib binary for Linux x86_64..."
    mkdir -p lib
    TDLIB_TMP="/tmp/tdlib_prebuilt.tgz"
    if curl -sSL -f "https://registry.npmjs.org/@prebuilt-tdlib/linux-x64-glibc/-/linux-x64-glibc-0.1008067.0.tgz" -o "$TDLIB_TMP"; then
        tar -xzf "$TDLIB_TMP" -C lib --strip-components=1 package/libtdjson.so
        rm -f "$TDLIB_TMP"
        ln -sf libtdjson.so lib/libtdjson.so.1.8.67 2>/dev/null || true

        if [ "$(id -u)" -eq 0 ]; then
            cp lib/libtdjson.so /usr/local/lib/
            ldconfig 2>/dev/null || true
        elif command -v sudo >/dev/null 2>&1; then
            sudo cp lib/libtdjson.so /usr/local/lib/ 2>/dev/null || true
            sudo ldconfig 2>/dev/null || true
        fi
        log_success "TDLib library installed successfully."
    else
        log_error "Failed to download TDLib binary. Please check internet connection."
        exit 1
    fi
fi

log_info "Checking NTgCalls voice library (libntgcalls.so)..."
NTGCALLS_FOUND=false
for dir in "/usr/local/lib" "/usr/lib" "/usr/lib/x86_64-linux-gnu" "$PWD/lib"; do
    if [ -f "$dir/libntgcalls.so" ]; then
        NTGCALLS_FOUND=true
        log_success "NTgCalls library found in $dir/libntgcalls.so"
        break
    fi
done

if [ "$NTGCALLS_FOUND" = false ]; then
    log_info "libntgcalls.so not found. Downloading official prebuilt NTgCalls for Linux x86_64..."
    mkdir -p lib include /tmp/ntgcalls_extracted
    NTGCALLS_TMP="/tmp/ntgcalls_prebuilt.zip"
    if curl -sSL -f "https://github.com/pytgcalls/ntgcalls/releases/download/v2.2.5/ntgcalls.linux-x86_64-shared_libs.zip" -o "$NTGCALLS_TMP"; then
        unzip -q -o "$NTGCALLS_TMP" -d /tmp/ntgcalls_extracted
        cp /tmp/ntgcalls_extracted/lib/libntgcalls.so lib/
        cp /tmp/ntgcalls_extracted/include/ntgcalls.h include/
        rm -rf "$NTGCALLS_TMP" /tmp/ntgcalls_extracted

        if [ "$(id -u)" -eq 0 ]; then
            cp lib/libntgcalls.so /usr/local/lib/
            ldconfig 2>/dev/null || true
        elif command -v sudo >/dev/null 2>&1; then
            sudo cp lib/libntgcalls.so /usr/local/lib/ 2>/dev/null || true
            sudo ldconfig 2>/dev/null || true
        fi
        log_success "NTgCalls voice streaming library installed successfully."
    else
        log_warn "Failed to download prebuilt NTgCalls library. Voice chat streaming may operate in signaling mode."
    fi
fi

ENV_FILE=".env"

if [ ! -f "$ENV_FILE" ]; then
    echo ""
    echo -e "${YELLOW}${BOLD}----------------------------------------------------------------------"
    echo "  No .env file found! Launching Interactive Setup Wizard...           "
    echo -e "----------------------------------------------------------------------${NC}"
    echo ""

    prompt_value() {
        local num="$1"
        local name="$2"
        local desc="$3"
        local var_name="$4"
        local default="$5"
        local user_val=""

        while true; do
            echo -e "${CYAN}${BOLD}[$num] Enter $name${NC} ($desc):"
            if [ -n "$default" ]; then
                read -r -p "    Default [$default]: " user_val
                user_val="${user_val:-$default}"
            else
                read -r -p "    > " user_val
            fi

            if [ -n "$user_val" ]; then
                eval "$var_name=\"\$user_val\""
                break
            else
                log_warn "$name cannot be empty. Please enter a valid value."
            fi
        done
    }

    prompt_value "1" "BOT_TOKEN" "Telegram Bot Token from @BotFather" INP_BOT_TOKEN
    prompt_value "2" "API_ID" "Telegram API ID from https://my.telegram.org" INP_API_ID
    prompt_value "3" "API_HASH" "Telegram API HASH from https://my.telegram.org" INP_API_HASH
    prompt_value "4" "OWNER_ID" "Your numeric Telegram User ID (e.g. from @userinfobot)" INP_OWNER_ID
    prompt_value "5" "LOGGER_ID" "Log Channel or Group Telegram ID (e.g. -100xxxxxxxxxx)" INP_LOGGER_ID

    echo -e "${CYAN}${BOLD}[6] Enter PHONE_NUMBER${NC} (Assistant phone with international code, e.g. +1234567890):"
    read -r -p "    > " INP_PHONE_NUMBER

    echo -e "${CYAN}${BOLD}[7] Enter COOKIES_URL / COOKIES_LINK${NC} (Batbin URL containing YouTube cookies, or press Enter to skip):"
    read -r -p "    > " INP_COOKIES_URL

    log_info "Creating .env file..."

    cat <<EOF > "$ENV_FILE"

API_ID=$INP_API_ID
API_HASH=$INP_API_HASH
BOT_TOKEN=$INP_BOT_TOKEN
OWNER_ID=$INP_OWNER_ID
LOGGER_ID=$INP_LOGGER_ID

SESSION_NAME=assistant
DATA_DIR=./data/tdlib_session
PHONE_NUMBER=$INP_PHONE_NUMBER

DB_PATH=senpai.db

DURATION_LIMIT=60
QUEUE_LIMIT=20
PLAYLIST_LIMIT=20
AUTO_LEAVE=False
AUTO_END=False
THUMB_GEN=True
VIDEO_PLAY=True
LANG_CODE=en

COOKIES_URL=$INP_COOKIES_URL

SUPPORT_CHANNEL=https://t.me/fallenx
SUPPORT_CHAT=https://t.me/DevilsHeavenMF
EOF

    log_success ".env configuration file created successfully."
else
    log_info "Existing .env file detected. Using existing configuration."
fi

# Fetch remote YouTube cookies if configured in .env (COOKIES_URL / COOKIES_LINK)
COOKIES_CFG=$(grep -E '^[[:space:]]*(COOKIES_URL|COOKIES_LINK|COOKIE_URL|COOKIE_LINK)=' "$ENV_FILE" 2>/dev/null | head -n 1 | cut -d '=' -f2- | tr -d '"' | tr -d "'" || true)
if [ -n "$COOKIES_CFG" ]; then
    mkdir -p cookies
    cookie_idx=1
    for raw_url in $COOKIES_CFG; do
        fetch_url="$raw_url"
        if echo "$fetch_url" | grep -q "batbin.me/" && ! echo "$fetch_url" | grep -q "batbin.me/raw/"; then
            fetch_url=$(echo "$fetch_url" | sed 's|batbin.me/|batbin.me/raw/|')
        elif echo "$fetch_url" | grep -q "pastebin.com/" && ! echo "$fetch_url" | grep -q "pastebin.com/raw/"; then
            fetch_url=$(echo "$fetch_url" | sed 's|pastebin.com/|pastebin.com/raw/|')
        elif echo "$fetch_url" | grep -q "hastebin.com/" && ! echo "$fetch_url" | grep -q "hastebin.com/raw/"; then
            fetch_url=$(echo "$fetch_url" | sed 's|hastebin.com/|hastebin.com/raw/|')
        fi

        log_info "Downloading YouTube cookies from $raw_url..."
        dest_file="cookies/cookie_${cookie_idx}.txt"
        if curl -sSL -f --max-time 15 "$fetch_url" -o "$dest_file"; then
            if [ -s "$dest_file" ] && ! grep -q "\[Batbin Error\]" "$dest_file" && ! head -n 2 "$dest_file" | grep -qi "<\!DOCTYPE\|<html"; then
                log_success "YouTube cookies saved to $dest_file ($(wc -c < "$dest_file" | tr -d ' ') bytes)"
                cookie_idx=$((cookie_idx + 1))
            else
                log_warn "Downloaded content from $raw_url was invalid (HTML error page); discarding."
                rm -f "$dest_file"
            fi
        else
            log_warn "Failed to download cookies from $raw_url"
            rm -f "$dest_file"
        fi
    done
fi

echo ""
log_info "Configuring and compiling senpai-cpp via CMake (SENPAI_WITH_TDLIB=ON)..."

mkdir -p build
NPROC=$(nproc 2>/dev/null || echo 4)

if ! cmake -B build -S . -DSENPAI_WITH_TDLIB=ON; then
    echo ""
    log_error "CMake configuration failed!"
    echo "Please check missing dependencies or CMakeLists.txt logs."
    exit 1
fi

if ! cmake --build build --target senpai -j"$NPROC"; then
    echo ""
    log_error "Build failed!"
    echo "If you encountered compiler or linker errors, please verify build-essential / g++ versions."
    exit 1
fi

log_success "Build completed successfully."

echo ""
echo -e "${GREEN}${BOLD}======================================================================${NC}"
echo -e "${GREEN}${BOLD}                   Starting SenpaiMusic (C++ Bot)                     ${NC}"
echo -e "${GREEN}${BOLD}======================================================================${NC}"
log_info "Launching bot process..."
echo -e "${YELLOW}NOTE: Check your Telegram app for the official login code and enter it below.${NC}"
echo ""

export LD_LIBRARY_PATH="/usr/local/lib:$PWD/lib:${LD_LIBRARY_PATH:-}"
ln -sf senpai ./build/anonx 2>/dev/null || true
exec ./build/senpai "$ENV_FILE"
