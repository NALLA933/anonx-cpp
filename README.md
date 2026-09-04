# AnonXMusic (C++ Port)

<div align="center">

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL2-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Build](https://img.shields.io/badge/Build-CMake-064F8C?style=for-the-badge&logo=cmake)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)

**Ultra-fast, low-memory Telegram Voice Chat Music Bot written in modern C++17.**

</div>

---

## 🚀 Highlights & Features

- **Blazing Performance & Minimal Footprint:** Consumes only ~15–30 MB RAM (down from 300 MB+ in Python), with near-instantaneous command dispatch.
- **Dual-Layer Architecture:**
  - **Telegram Bot API:** High-speed handling of group chat commands, interactive inline buttons, and permission guards.
  - **MTProto Assistant Userbot (TDLib):** Manages Telegram voice chat (VC) participation with persistent disk sessions.
- **Embedded SQLite Persistence:** High-speed embedded database with WAL mode and in-memory write-through caching. Zero external database servers required.
- **Robust Subprocess Media Streaming:** Secure shell-escaped `ffmpeg` and `yt-dlp` pipeline with resilient YouTube streaming and playlist extraction.
- **Multilingual Support:** Native translation engine supporting 13 languages loaded directly from clean locale templates.
- **Interactive First-Run Wizard:** 1-click automatic dependency installation, guided `.env` setup, automated CMake build, and interactive console OTP authentication.

---

## ⚡ Quick Start (Beginner 1-Click Deploy)

Deploying `anonx-cpp` on Ubuntu, Debian, or WSL2 is completely automated using the included `start.sh` runner.

### 3-Step Setup

```bash
# 1. Clone the repository
git clone https://github.com/TeamFallen/AnonXMusic.git anonx-cpp

# 2. Enter directory
cd anonx-cpp

# 3. Run 1-click installer
./start.sh
```

### What `start.sh` Does Automatically:
1. **Checks & Installs Dependencies:** Detects missing packages (`cmake`, `g++`, `make`, `ffmpeg`, `libsqlite3-dev`, `nlohmann-json3-dev`, `libssl-dev`) and installs them via `apt-get`.
2. **Interactive Configuration Wizard:** If `.env` is not found, prompts you directly in the terminal for your credentials (`BOT_TOKEN`, `API_ID`, `API_HASH`, `OWNER_ID`, `LOGGER_ID`, `PHONE_NUMBER`) and creates the configuration file.
3. **Automated CMake Compilation:** Configures and compiles the bot using all available CPU cores.
4. **Interactive Execution:** Launches the bot with standard input attached so you can enter Telegram login OTP codes on first run.

---

## ⚙️ Manual Build & Advanced Setup

If you prefer to configure and compile manually, follow the steps below.

### 1. Install System Dependencies

On Ubuntu / Debian / WSL2:

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ make ffmpeg libsqlite3-dev nlohmann-json3-dev libssl-dev yt-dlp
```

### 2. Configure Environment (`.env`)

Copy `sample.env` to `.env` and fill in your credentials:

```bash
cp sample.env .env
nano .env
```

#### Configuration Breakdown

| Variable | Required | Description | Example |
|---|:---:|---|---|
| `BOT_TOKEN` | **Yes** | Telegram Bot Token from [@BotFather](https://t.me/BotFather) | `7123456789:AAH...` |
| `API_ID` | **Yes** | Telegram App API ID from [my.telegram.org](https://my.telegram.org) | `1234567` |
| `API_HASH` | **Yes** | Telegram App API Hash from [my.telegram.org](https://my.telegram.org) | `abcdef0123456789...` |
| `OWNER_ID` | **Yes** | Numeric Telegram ID of the bot owner | `123456789` |
| `LOGGER_ID` | **Yes** | Telegram ID of the private logging channel/group | `-1001234567890` |
| `PHONE_NUMBER` | Optional | Phone number for the assistant account | `+1234567890` |
| `SESSION_NAME` | Optional | Identifier for the assistant session | `assistant` |
| `DATA_DIR` | Optional | Directory where TDLib stores session keys | `./data/tdlib_session` |
| `DB_PATH` | Optional | SQLite database path | `anonx.db` |
| `DURATION_LIMIT` | Optional | Max allowed track duration in minutes | `60` |
| `QUEUE_LIMIT` | Optional | Max tracks in per-chat queue | `20` |
| `AUTO_LEAVE` | Optional | Auto-leave voice chat when empty (`True`/`False`) | `False` |
| `AUTO_END` | Optional | Auto-stop stream when no listeners (`True`/`False`) | `False` |
| `LANG_CODE` | Optional | Default locale language code | `en` |

### 3. Compile with CMake

```bash
# Configure build directory
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Compile all targets in parallel
cmake --build build -j$(nproc)

# Run offline unit test suites
ctest --test-dir build --output-on-failure
```

### 4. Launch the Bot

```bash
./build/anonx .env
```

---

## 📱 First-Time Login Guide (TDLib Authentication)

Unlike legacy Python music bots, TDLib does **not** use Pyrogram base64 string sessions. It uses native MTProto authentication and stores session encryption keys locally on disk.

1. On first startup, the console will request the assistant's phone number if `PHONE_NUMBER` is not already specified in `.env`:
   ```text
   Enter phone number for AnonyUB1 (e.g. +1234567890):
   ```
2. Telegram will send a login verification code via official Telegram notification or SMS:
   ```text
   Enter login code for AnonyUB1: 12345
   ```
3. If your account has Two-Step Verification (Cloud Password) enabled, provide your password:
   ```text
   Enter 2FA password for AnonyUB1 (blank if none): your_password
   ```
4. Once authenticated, TDLib stores the session keys inside `./data/tdlib_session`. Subsequent restarts log in **automatically** without requiring prompts!

---

## 📋 Bot Commands

### 🎵 Playback Commands (Group Chats)

| Command | Aliases | Description |
|---|---|---|
| `/play <query/url>` | `/vplay` | Stream audio or video in the group voice chat |
| `/playforce <query>` | `/vplayforce` | Force play a track immediately, clearing queue |
| `/pause` | - | Pause ongoing voice chat playback |
| `/resume` | - | Resume paused voice chat playback |
| `/skip` | - | Skip to the next queued track |
| `/end` | `/stop` | Stop playback and clear the queue |
| `/loop <1-10>` | - | Loop current playing track N times |
| `/seek <seconds>` | - | Fast forward playback by specified seconds |
| `/queue` | - | Display upcoming tracks in current group chat |

### 🛡️ Admin & Moderation Commands

| Command | Scope | Description |
|---|---|---|
| `/auth` / `/unauth` | Admins | Authorize/unauthorize a non-admin to manage music |
| `/authlist` | Everyone | View authorized users in current group |
| `/blacklist` / `/unblacklist` | Sudo | Blacklist or whitelist a user or chat |
| `/addsudo` / `/rmsudo` | Owner | Add or remove bot sudoers |
| `/sudolist` | Everyone | View list of bot sudoers |
| `/lang` / `/language` | Admins | Change bot language for current chat |
| `/ping` / `/alive` | Everyone | Check bot latency and host system metrics |
| `/stats` / `/gstats` | Everyone | Display active streams, memory usage, and served chats |
| `/ac` / `/activevc` | Sudo | View all active group voice chats |
| `/gcast <message>` | Sudo | Broadcast message to all served chats |

---

## 📂 Repository Structure

```
anonx-cpp/
├── CMakeLists.txt             # Build system configuration
├── start.sh                   # 1-click installer and interactive runner
├── sample.env                 # Template environment variables
├── include/anonx/             # Public C++ headers
│   ├── config.hpp             # Configuration loader & validation
│   ├── database.hpp           # Embedded SQLite data layer
│   ├── telegram_client.hpp    # High-level account client (Bot & Userbot)
│   ├── td_client.hpp          # TDLib JSON transport wrapper
│   ├── dispatcher.hpp         # Event routing & filters
│   ├── userbot.hpp            # Assistant account manager
│   ├── call_manager.hpp       # Voice chat queue & playback orchestration
│   ├── plugins.hpp            # Music playback commands
│   ├── admin_plugins.hpp      # Admin, menu, and system tool commands
│   └── youtube.hpp            # yt-dlp subprocess integration
├── src/                       # Implementation source files
│   ├── main.cpp               # Bot application entrypoint
│   ├── config.cpp             # .env parser and validator
│   ├── telegram_client.cpp    # TDLib auth state machine
│   ├── ntgcalls_transport.cpp # NTgCalls voice streaming engine
│   └── ...
├── locales/                   # 13 language JSON dictionaries
└── test/                      # Offline mock scaffolding & unit test suites
```

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
Built with passion by the **AnonX** open-source team.
