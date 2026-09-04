# 🎵 AnonX-CPP: High-Performance C++20 Telegram Music Bot

<div align="center">

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=c%2B%2B)
![Build](https://img.shields.io/badge/Build-CMake%203.25%2B%20%7C%20Ninja-064F8C?style=for-the-badge&logo=cmake)
![Package Manager](https://img.shields.io/badge/Package%20Manager-vcpkg%20(Manifest)-5C2D91?style=for-the-badge)
![Docker](https://img.shields.io/badge/Docker-GHCR%20Multi--Arch-2496ED?style=for-the-badge&logo=docker)
![Memory Safe](https://img.shields.io/badge/RAM%20Footprint-%3C%20150%20MB-success?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)

**Next-generation, ultra-efficient Telegram Voice Chat Music Bot written in modern C++20.**  
*Engineered specifically to run flawlessly on low-end servers (1 vCPU, 1 GB RAM VPS) with **Zero-VPS Compilation**.*

</div>

---

## ⚡ Why AnonX-CPP?

Building large C++ applications (TDLib, MongoDB C++ Driver, WebRTC/NTgCalls) on low-spec $3/month VPS instances fails with `cc1plus: fatal error: Killed (Out of Memory)`. 

**AnonX-CPP solves this permanently by offloading 100% of compilation to GitHub Actions cloud runners.**

| Feature | Legacy Python Bots | Other C++ Bots | **AnonX-CPP** |
|---|:---:|:---:|:---:|
| **Language & Standard** | Python 3.10+ | C++14 / C++17 | **Modern C++20** |
| **RAM Usage** | 350 MB – 800 MB | 150 MB – 300 MB | **< 120 MB** |
| **Install Time on 1GB VPS** | 5 – 10 minutes | 45 – 90 minutes (OOM Risk) | **< 20 seconds (Zero Compilation)** |
| **Compilation Overhead** | None (Interpreted) | Extreme (Kills VPS) | **0% on VPS (Prebuilt CI/CD)** |
| **Database** | MongoDB / SQLite | Local SQLite | **MongoDB C++ Pool + Resilient Fallback** |
| **Audio Engine** | PyTgCalls (Python GIL) | Raw Pipes | **FFmpeg Subprocess + NTgCalls WebRTC** |
| **yt-dlp Maintenance** | Host `pip` conflicts | Broken system bins | **Auto-updating standalone binary** |

---

## 🚀 Quick Deployment (Zero Compilation)

### Method 1: Docker Compose (Recommended - 20 Seconds)

Ensure Docker and Docker Compose are installed on your VPS.

```bash
# 1. Download configuration and docker-compose files
curl -sSL https://raw.githubusercontent.com/NALLA933/anonx-cpp/main/docker-compose.yml -o docker-compose.yml
curl -sSL https://raw.githubusercontent.com/NALLA933/anonx-cpp/main/config.example.json -o config.json

# 2. Add your Telegram Bot Token & API Credentials
nano config.json

# 3. Start AnonX and MongoDB containers in the background
docker compose up -d

# 4. View live streaming logs
docker compose logs -f anonx-bot
```

#### Updating Docker Deployment:
```bash
docker compose pull && docker compose up -d
```

---

### Method 2: Bare-Metal VPS (Systemd 1-Command Installer - 10 Seconds)

For users who prefer running directly on Ubuntu/Debian without Docker:

```bash
# Run the 1-command installer script
curl -sSL https://raw.githubusercontent.com/NALLA933/anonx-cpp/main/scripts/install.sh | bash
```

**What the installer does automatically:**
1. Detects CPU architecture (`x86_64` or `aarch64`).
2. Installs lightweight runtime dependencies (`ffmpeg`, `curl`, `python3`).
3. Fetches the latest standalone `yt-dlp` binary to `/usr/local/bin/yt-dlp`.
4. Downloads the pre-compiled stripped native binary from GitHub Releases into `/opt/anonx/`.
5. Configures and registers the `anonx.service` systemd daemon with automatic failure restarts.

```bash
# Configure bot credentials
nano /opt/anonx/config.json

# Start and enable the service
sudo systemctl enable --now anonx

# Monitor logs
sudo journalctl -u anonx -f
```

#### Updating Bare-Metal Deployment:
```bash
curl -sSL https://raw.githubusercontent.com/NALLA933/anonx-cpp/main/scripts/update.sh | bash
```

---

## ⚙️ Configuration Reference (`config.json`)

```json
{
  "bot_token": "123456789:ABCDefGhIjKlMnOpQrStUvWxYz",
  "api_id": 1234567,
  "api_hash": "abcdef1234567890abcdef1234567890",
  "string_session": "",
  "owner_id": 1234567890,
  "sudo_users": [1234567890],
  "mongo_uri": "mongodb://localhost:27017/anonx",
  "db_name": "anonx",
  "log_group_id": -1001234567890,
  "audio_quality": "high",
  "duration_limit_sec": 7200,
  "log_level": "info",
  "auto_update_ytdlp": true
}
```

*Note: All settings can also be overridden via standard environment variables (`BOT_TOKEN`, `API_ID`, `API_HASH`, `STRING_SESSION`, `MONGO_URI`, `OWNER_ID`, `SUDO_USERS`).*

---

## 📋 Bot Commands

### 🎵 Music & Voice Chat Controls

| Command | Aliases | Description |
|---|---|---|
| `/play <query or URL>` | `/p` | Stream audio from YouTube, audio URLs, or search terms |
| `/pause` | - | Pause ongoing voice chat stream |
| `/resume` | - | Resume paused audio stream |
| `/skip` | `/next` | Skip to the next song in the MongoDB queue |
| `/stop` | `/end` | Stop playback, clear queue, and leave voice chat |
| `/queue` | `/q` | Display the active song queue |
| `/volume <1-200>` | `/vol` | Adjust live playback volume with real-time PCM gain scaling |
| `/ping` | - | Check latency, uptime, and host resource status |
| `/help` | `/start` | View list of available commands |

---

## 🏗️ Technical Architecture

The project is structured as a **Modular Layered Monolith** in modern C++20:

```
include/anonx/  &  src/
├── core/
│   ├── config.hpp / .cpp         # JSON & environment variable loader
│   ├── logger.hpp / .cpp         # spdlog integration with ANSI console fallback
│   ├── safe_queue.hpp            # Thread-safe concurrent FIFO queue with CV sync
│   └── thread_pool.hpp / .cpp    # C++20 invocable worker pool returning std::future
├── database/
│   ├── models.hpp / .cpp         # TrackItem, ChatSettings, SudoEntry serialization
│   └── mongo_client.hpp / .cpp   # PIMPL MongoDB C++ driver wrapper + in-memory store
├── audio/
│   ├── ffmpeg_pipeline.hpp / .cpp# 48kHz stereo PCM streaming pipeline
│   ├── ntgcalls_client.hpp / .cpp# Resilient dynamic WebRTC voice bridge
│   └── audio_streamer.hpp / .cpp # Playback coordinator with PCM gain volume scaling
├── telegram/
│   ├── tdlib_client.hpp / .cpp   # Asynchronous TDLib JSON client wrapper
│   ├── session_manager.hpp / .cpp# Dual-session Bot & Assistant coordinator
│   └── dispatcher.hpp / .cpp     # Command parser and event router
└── main.cpp                      # Signal traps (SIGINT/SIGTERM, SIGPIPE ignore) & bootstrap
```

### Build Acceleration:
- **`vcpkg` Manifest Mode**: Complete dependency graph declared in `vcpkg.json`.
- **`mold` / `lld` Linker**: Sub-second linking managed by `cmake/MoldLinker.cmake`.
- **`ccache`**: 90% build time reduction during CI runs and local development.
- **Docker Multi-Stage**: Builder stage with Ninja/Mold -> Slim runtime stage (< 180 MB).

---

## 🛠️ Local Development & Contributing

To build and contribute locally:

```bash
# 1. Clone repository
git clone https://github.com/NALLA933/anonx-cpp.git
cd anonx-cpp

# 2. Install dependencies via vcpkg
vcpkg install

# 3. Configure with CMake
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# 4. Build executable
cmake --build build --target anonx_bot -j$(nproc)
```

---

## 📄 License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for details.
Built with modern C++ standards for the global Telegram community.
