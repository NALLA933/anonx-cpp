#include <anonx/audio/ffmpeg_pipeline.hpp>
#include <anonx/core/logger.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <mutex>
#include <sstream>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace anonx::audio {

namespace {

struct CacheEntry {
    std::string url;
    std::chrono::steady_clock::time_point expires_at;
};

class StreamUrlCache {
public:
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (it->second.expires_at > std::chrono::steady_clock::now()) {
                return it->second.url;
            }
            cache_.erase(it);
        }
        return std::nullopt;
    }

    void put(const std::string& key, const std::string& val, std::chrono::seconds ttl = std::chrono::hours(2)) {
        std::lock_guard<std::mutex> lock(mtx_);
        cache_[key] = CacheEntry{val, std::chrono::steady_clock::now() + ttl};
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        cache_.clear();
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, CacheEntry> cache_;
};

StreamUrlCache& get_url_cache() {
    static StreamUrlCache cache;
    return cache;
}

} // namespace

FFmpegPipeline::FFmpegPipeline() = default;

FFmpegPipeline::~FFmpegPipeline() {
    stop();
}

std::string FFmpegPipeline::resolve_stream_url(const std::string& input_query) {
    // If it's already a direct audio file or HTTP stream, return as is
    if (input_query.rfind("http://", 0) == 0 || input_query.rfind("https://", 0) == 0) {
        if (input_query.find("youtube.com") == std::string::npos &&
            input_query.find("youtu.be") == std::string::npos) {
            return input_query;
        }
    } else if (input_query.find("://") == std::string::npos && input_query.find('.') != std::string::npos) {
        // Local file path
        return input_query;
    }

    // High-concurrency TTL Cache Lookup (eliminates duplicate yt-dlp invocations)
    if (auto cached = get_url_cache().get(input_query); cached.has_value()) {
        ANONX_LOG_INFO("FFmpegPipeline", "Cache hit for stream URL: ", input_query);
        return *cached;
    }

    std::string query = input_query;
    if (query.rfind("http://", 0) != 0 && query.rfind("https://", 0) != 0) {
        query = "ytsearch1:" + input_query;
    }

    std::string cookie_arg;
    for (const auto& cpath : {"cookies.txt", "cookies/cookies.txt", "cookies/intergraft.txt", "cookies/deejay.txt"}) {
        if (std::filesystem::exists(cpath)) {
            cookie_arg = std::string("--cookies \"") + cpath + "\" ";
            break;
        }
    }

    // Use yt-dlp to extract direct audio streaming URL
    std::string command;
#if defined(_WIN32)
    command = "yt-dlp " + cookie_arg + "-f bestaudio -g \"" + query + "\" 2>nul";
#else
    command = "export PATH=\"$HOME/.deno/bin:$PATH:/usr/local/bin\"; yt-dlp " + cookie_arg + "-f bestaudio -g \"" + query + "\" 2>/dev/null";
#endif

    ANONX_LOG_INFO("FFmpegPipeline", "Extracting stream URL via yt-dlp: ", query);

    std::array<char, 512> buffer{};
    std::string result;

#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif

    if (!pipe) {
        ANONX_LOG_ERROR("FFmpegPipeline", "Failed to invoke yt-dlp subprocess.");
        return input_query;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Strip trailing whitespace / newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }

    if (result.empty()) {
        ANONX_LOG_WARN("FFmpegPipeline", "yt-dlp returned empty stream; falling back to original query: ", input_query);
        return input_query;
    }

    // Cache successful resolution for 2 hours
    get_url_cache().put(input_query, result, std::chrono::hours(2));
    return result;
}

void FFmpegPipeline::clear_stream_url_cache() {
    get_url_cache().clear();
}

bool FFmpegPipeline::start(const std::string& source_uri,
                           FrameCallback on_frame,
                           EofCallback on_eof,
                           ErrorCallback on_error) {
    if (is_running_) {
        stop();
    }

    is_running_ = true;
    is_paused_ = false;

    worker_thread_ = std::thread(&FFmpegPipeline::reader_thread_loop, this,
                                 source_uri,
                                 std::move(on_frame),
                                 std::move(on_eof),
                                 std::move(on_error));
    return true;
}

void FFmpegPipeline::stop() {
    is_running_ = false;

#if defined(_WIN32)
    if (process_handle_) {
        TerminateProcess(static_cast<HANDLE>(process_handle_), 0);
        CloseHandle(static_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }
    if (pipe_read_) {
        CloseHandle(static_cast<HANDLE>(pipe_read_));
        pipe_read_ = nullptr;
    }
#else
    if (child_pid_ > 0) {
        kill(-child_pid_, SIGTERM);
        int status = 0;
        waitpid(child_pid_, &status, WNOHANG);
        child_pid_ = -1;
    }
    if (pipe_fd_ >= 0) {
        close(pipe_fd_);
        pipe_fd_ = -1;
    }
#endif

    if (worker_thread_.joinable()) {
        if (std::this_thread::get_id() == worker_thread_.get_id()) {
            worker_thread_.detach();
        } else {
            worker_thread_.join();
        }
    }
}

void FFmpegPipeline::pause() {
    is_paused_ = true;
}

void FFmpegPipeline::resume() {
    is_paused_ = false;
}

bool FFmpegPipeline::is_running() const noexcept {
    return is_running_;
}

bool FFmpegPipeline::is_paused() const noexcept {
    return is_paused_;
}

void FFmpegPipeline::reader_thread_loop(const std::string& source_uri,
                                       FrameCallback on_frame,
                                       EofCallback on_eof,
                                       ErrorCallback on_error) {
    std::string cookie_arg;
    for (const auto& cpath : {"cookies.txt", "cookies/cookies.txt", "cookies/intergraft.txt", "cookies/deejay.txt"}) {
        if (std::filesystem::exists(cpath)) {
            cookie_arg = std::string("--cookies \"") + cpath + "\" ";
            break;
        }
    }

    std::string pipeline_cmd;
    bool is_yt = (source_uri.rfind("http://", 0) == 0 || source_uri.rfind("https://", 0) == 0 ||
                  (source_uri.find("://") == std::string::npos && source_uri.find('.') == std::string::npos));

    if (is_yt) {
        std::string query = source_uri;
        if (query.rfind("http://", 0) != 0 && query.rfind("https://", 0) != 0) {
            query = "ytsearch1:" + source_uri;
        }
        pipeline_cmd = "export PATH=\"$HOME/.deno/bin:$PATH:/usr/local/bin\"; yt-dlp " + cookie_arg +
                       "-f bestaudio -o - \"" + query + "\" 2>/dev/null | ffmpeg -i pipe:0 -f s16le -ac 2 -ar 48000 -acodec pcm_s16le -loglevel quiet pipe:1";
    } else {
        pipeline_cmd = "ffmpeg -i \"" + source_uri + "\" -f s16le -ac 2 -ar 48000 -acodec pcm_s16le -loglevel quiet pipe:1";
    }

    ANONX_LOG_INFO("FFmpegPipeline", "Spawning audio stream pipeline for: ", source_uri);

#if defined(_WIN32)
    HANDLE h_read = nullptr;
    HANDLE h_write = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&h_read, &h_write, &sa, 0)) {
        if (on_error) on_error("Failed to create process pipe on Windows.");
        is_running_ = false;
        return;
    }
    SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = h_write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};

    std::string cmdline = "cmd.exe /c \"" + pipeline_cmd + "\"";

    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(h_read);
        CloseHandle(h_write);
        if (on_error) on_error("Failed to execute FFmpeg child process.");
        is_running_ = false;
        return;
    }

    CloseHandle(h_write);
    CloseHandle(pi.hThread);
    process_handle_ = pi.hProcess;
    pipe_read_ = h_read;

    std::vector<uint8_t> frame_buffer(PCM_FRAME_BYTES);
    DWORD bytes_read = 0;

    while (is_running_) {
        if (is_paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        DWORD total_read = 0;
        while (total_read < PCM_FRAME_BYTES && is_running_) {
            BOOL success = ReadFile(h_read, frame_buffer.data() + total_read,
                                    static_cast<DWORD>(PCM_FRAME_BYTES - total_read),
                                    &bytes_read, nullptr);
            if (!success || bytes_read == 0) {
                break;
            }
            total_read += bytes_read;
        }

        if (total_read == 0) {
            break; // EOF
        }

        if (on_frame && is_running_) {
            on_frame(frame_buffer.data(), total_read);
        }
    }

    CloseHandle(h_read);
    pipe_read_ = nullptr;
    if (process_handle_) {
        CloseHandle(static_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }
#else
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        if (on_error) on_error("Failed to allocate pipe descriptors.");
        is_running_ = false;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (on_error) on_error("Failed to fork FFmpeg process.");
        is_running_ = false;
        return;
    }

    if (pid == 0) {
        // Child Process: set own process group so killpg cleanly cleans up both yt-dlp & ffmpeg
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execl("/bin/sh", "sh", "-c", pipeline_cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    // Parent Process
    close(pipefd[1]);
    pipe_fd_ = pipefd[0];
    child_pid_ = pid;

    std::vector<uint8_t> frame_buffer(PCM_FRAME_BYTES);

    while (is_running_) {
        if (is_paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        size_t total_read = 0;
        while (total_read < PCM_FRAME_BYTES && is_running_) {
            ssize_t bytes_read = read(pipe_fd_, frame_buffer.data() + total_read,
                                      PCM_FRAME_BYTES - total_read);
            if (bytes_read <= 0) {
                break;
            }
            total_read += static_cast<size_t>(bytes_read);
        }

        if (total_read == 0) {
            break; // Stream completed (EOF)
        }

        if (on_frame && is_running_) {
            on_frame(frame_buffer.data(), total_read);
        }
    }

    if (pipe_fd_ >= 0) {
        close(pipe_fd_);
        pipe_fd_ = -1;
    }

    if (child_pid_ > 0) {
        int status = 0;
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }
#endif

    is_running_ = false;
    if (on_eof) {
        on_eof();
    }
}

} // namespace anonx::audio
