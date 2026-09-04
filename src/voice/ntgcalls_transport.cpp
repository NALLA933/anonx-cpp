#include "senpai/ntgcalls_transport.hpp"

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include <nlohmann/json.hpp>
#include "ntgcalls.h"
#include "senpai/logger.hpp"

namespace senpai {
namespace {

Logger log() { return Logger("senpai.voice.ntgcalls"); }

std::string shellEscape(const std::string& s) {
    std::string r = "'";
    for (const char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    r += "'";
    return r;
}

typedef uintptr_t (*fn_ntg_init)();
typedef int (*fn_ntg_destroy)(uintptr_t);
typedef int (*fn_ntg_create)(uintptr_t, int64_t, char**, ntg_async_struct);
typedef int (*fn_ntg_connect)(uintptr_t, int64_t, char*, bool, ntg_async_struct);
typedef int (*fn_ntg_set_stream_sources)(uintptr_t, int64_t, ntg_stream_mode_enum, ntg_media_description_struct, ntg_async_struct);
typedef int (*fn_ntg_pause)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_resume)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_stop)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_on_stream_end)(uintptr_t, ntg_stream_callback, void*);
typedef int (*fn_ntg_on_connection_change)(uintptr_t, ntg_connection_callback, void*);

struct NtgLibrary {
    void* handle = nullptr;
    fn_ntg_init ntg_init = nullptr;
    fn_ntg_destroy ntg_destroy = nullptr;
    fn_ntg_create ntg_create = nullptr;
    fn_ntg_connect ntg_connect = nullptr;
    fn_ntg_set_stream_sources ntg_set_stream_sources = nullptr;
    fn_ntg_pause ntg_pause = nullptr;
    fn_ntg_resume ntg_resume = nullptr;
    fn_ntg_stop ntg_stop = nullptr;
    fn_ntg_on_stream_end ntg_on_stream_end = nullptr;
    fn_ntg_on_connection_change ntg_on_connection_change = nullptr;

    bool loaded() const { return handle != nullptr && ntg_init != nullptr; }

    static NtgLibrary& instance() {
        static NtgLibrary lib;
        return lib;
    }

private:
    NtgLibrary() {
#if !defined(_WIN32)
        const char* paths[] = {
            "libntgcalls.so",
            "./lib/libntgcalls.so",
            "/usr/local/lib/libntgcalls.so",
            "/usr/lib/libntgcalls.so",
            "/usr/lib/x86_64-linux-gnu/libntgcalls.so",
            nullptr
        };
        for (int i = 0; paths[i] != nullptr; ++i) {
            handle = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                log().info(std::string("loaded NTgCalls library from: ") + paths[i]);
                break;
            }
        }
        if (!handle) {
            log().warning("libntgcalls.so not found — streaming will run in signaling-only mode");
            return;
        }

        ntg_init = (fn_ntg_init)dlsym(handle, "ntg_init");
        ntg_destroy = (fn_ntg_destroy)dlsym(handle, "ntg_destroy");
        ntg_create = (fn_ntg_create)dlsym(handle, "ntg_create");
        ntg_connect = (fn_ntg_connect)dlsym(handle, "ntg_connect");
        ntg_set_stream_sources = (fn_ntg_set_stream_sources)dlsym(handle, "ntg_set_stream_sources");
        ntg_pause = (fn_ntg_pause)dlsym(handle, "ntg_pause");
        ntg_resume = (fn_ntg_resume)dlsym(handle, "ntg_resume");
        ntg_stop = (fn_ntg_stop)dlsym(handle, "ntg_stop");
        ntg_on_stream_end = (fn_ntg_on_stream_end)dlsym(handle, "ntg_on_stream_end");
        ntg_on_connection_change = (fn_ntg_on_connection_change)dlsym(handle, "ntg_on_connection_change");

        typedef void (*fn_ntg_register_logger)(ntg_log_message_callback);
        fn_ntg_register_logger ntg_register_logger =
            (fn_ntg_register_logger)dlsym(handle, "ntg_register_logger");
        if (ntg_register_logger) {
            ntg_register_logger([](ntg_log_message_struct msg) {
                if (msg.message && (msg.level == NTG_LOG_ERROR || msg.level == NTG_LOG_WARNING)) {
                    // Ignore benign WebRTC local audio recording initialization warning on headless servers
                    std::string text = msg.message;
                    if (text.find("Failed to initialize recording") != std::string::npos) {
                        return;
                    }
                    if (msg.level == NTG_LOG_ERROR) {
                        log().error(std::string("[NTgCalls] ") + text);
                    } else {
                        log().warning(std::string("[NTgCalls] ") + text);
                    }
                }
            });
        }

        if (!ntg_init || !ntg_create || !ntg_connect || !ntg_set_stream_sources) {
            log().error("libntgcalls.so missing required symbol exports");
            dlclose(handle);
            handle = nullptr;
        }
#endif
    }
};

struct AsyncWait : std::enable_shared_from_this<AsyncWait> {
    std::promise<void> prom;
    int errCode = 0;
    char* errMsg = nullptr;
    std::atomic<bool> resolved{false};

    static void callback(void* user) {
        auto* holder = static_cast<std::shared_ptr<AsyncWait>*>(user);
        if (holder) {
            auto self = *holder;
            delete holder;
            if (self && !self->resolved.exchange(true)) {
                try {
                    self->prom.set_value();
                } catch (...) {}
            }
        }
    }

    ntg_async_struct makeFuture(std::shared_ptr<AsyncWait> self) {
        ntg_async_struct s = {};
        auto* holder = new std::shared_ptr<AsyncWait>(self);
        s.userData = holder;
        s.errorCode = &self->errCode;
        s.errorMessage = &self->errMsg;
        s.promise = &callback;
        return s;
    }

    bool wait(int timeoutSec = 15) {
        auto fut = prom.get_future();
        if (fut.wait_for(std::chrono::seconds(timeoutSec)) == std::future_status::timeout) {
            return false;
        }
        if (errCode != 0) {
            std::string msg = errMsg ? errMsg : ("ntgcalls error code " + std::to_string(errCode));
            throw std::runtime_error(msg);
        }
        return true;
    }
};

struct StreamContext {
    std::string audioCmd;
    std::string videoCmd;
    ntg_audio_description_struct audioDesc{};
    ntg_video_description_struct videoDesc{};
    ntg_media_description_struct mediaDesc{};
};

} // namespace

struct NtgCallsTransport::Impl {
    explicit Impl(Signaling sig) : signaling(std::move(sig)) {
        auto& lib = NtgLibrary::instance();
        if (lib.loaded()) {
            client = lib.ntg_init();
            if (lib.ntg_on_stream_end) {
                lib.ntg_on_stream_end(client, &onStreamEndStatic, this);
            }
            if (lib.ntg_on_connection_change) {
                lib.ntg_on_connection_change(client, &onConnectionChangeStatic, this);
            }
        }
    }

    ~Impl() {
        auto& lib = NtgLibrary::instance();
        {
            std::lock_guard<std::mutex> lk(mtx);
            activeStreams.clear();
        }
        if (lib.loaded() && client != 0 && lib.ntg_destroy) {
            lib.ntg_destroy(client);
            client = 0;
        }
    }

    static void onStreamEndStatic(uintptr_t, int64_t chatId, ntg_stream_type_enum type,
                                  ntg_stream_device_enum, void* userData) {
        auto* self = static_cast<Impl*>(userData);
        if (self && self->onStreamEnd && type == NTG_STREAM_AUDIO) {
            self->onStreamEnd(chatId, StreamKind::Audio);
        }
    }

    static void onConnectionChangeStatic(uintptr_t, int64_t chatId,
                                         ntg_network_info_struct info, void* userData) {
        auto* self = static_cast<Impl*>(userData);
        if (self && self->onCallClosed) {
            if (info.state == NTG_STATE_CLOSED || info.state == NTG_STATE_FAILED ||
                info.state == NTG_STATE_TIMEOUT) {
                self->onCallClosed(chatId);
            }
        }
    }

    uintptr_t client = 0;
    Signaling signaling;
    mutable std::mutex mtx;
    std::unordered_map<int64_t, std::shared_ptr<StreamContext>> activeStreams;
    StreamEndHandler onStreamEnd;
    CallClosedHandler onCallClosed;
};

NtgCallsTransport::NtgCallsTransport(Signaling signaling)
    : impl_(std::make_unique<Impl>(std::move(signaling))) {}

NtgCallsTransport::~NtgCallsTransport() = default;

PlayResult NtgCallsTransport::play(std::int64_t chatId, const MediaSource& src) {
    auto& lib = NtgLibrary::instance();

    try {
        std::string localParams;

        if (lib.loaded() && impl_->client != 0) {
            char* buffer = nullptr;
            auto createWait = std::make_shared<AsyncWait>();
            int rc = lib.ntg_create(impl_->client, chatId, &buffer,
                                    createWait->makeFuture(createWait));
            if (rc != 0 || !createWait->wait(15) || !buffer) {
                log().warning("ntg_create failed for chat " + std::to_string(chatId));
                return PlayResult::ServerError;
            }
            localParams = buffer;
            log().info("ntg_create generated WebRTC parameters (" +
                      std::to_string(localParams.size()) + " bytes)");
        } else {
            nlohmann::json dummy;
            dummy["ssrc"] = 1;
            dummy["audio_source"] = 1;
            dummy["source"] = 1;
            localParams = dummy.dump();
        }

        std::string remoteParams;
        if (impl_->signaling.joinGroupCall) {
            remoteParams = impl_->signaling.joinGroupCall(chatId, localParams);
        } else {
            return PlayResult::ServerError;
        }

        if (lib.loaded() && impl_->client != 0) {
            auto connectWait = std::make_shared<AsyncWait>();
            log().info("connecting NTgCalls WebRTC engine to Telegram server for chat " +
                       std::to_string(chatId));
            int rc = lib.ntg_connect(impl_->client, chatId,
                                     const_cast<char*>(remoteParams.c_str()), false,
                                     connectWait->makeFuture(connectWait));
            if (rc != 0 || !connectWait->wait(15)) {
                log().warning("ntg_connect failed for chat " + std::to_string(chatId));
                return PlayResult::ServerError;
            }

            auto ctx = std::make_shared<StreamContext>();
            const std::string seek =
                src.seekSeconds > 1 ? ("-ss " + std::to_string(src.seekSeconds) + " ") : "";
            ctx->audioCmd = "ffmpeg -nostdin " + seek + "-i " + shellEscape(src.path) +
                            " -f s16le -ac 2 -ar 48000 -loglevel quiet pipe:1";

            ctx->audioDesc.mediaSource = NTG_SHELL;
            ctx->audioDesc.input = const_cast<char*>(ctx->audioCmd.c_str());
            ctx->audioDesc.sampleRate = 48000;
            ctx->audioDesc.channelCount = 2;
            ctx->audioDesc.keepOpen = false;

            ctx->mediaDesc.microphone = &ctx->audioDesc;

            if (src.video) {
                ctx->videoCmd = "ffmpeg -nostdin " + seek + "-i " + shellEscape(src.path) +
                               " -f rawvideo -pix_fmt yuv420p -vf scale=1280:720 -r 30 -loglevel quiet pipe:1";
                ctx->videoDesc.mediaSource = NTG_SHELL;
                ctx->videoDesc.input = const_cast<char*>(ctx->videoCmd.c_str());
                ctx->videoDesc.width = 1280;
                ctx->videoDesc.height = 720;
                ctx->videoDesc.fps = 30;
                ctx->videoDesc.keepOpen = false;
                ctx->mediaDesc.camera = &ctx->videoDesc;
            }

            {
                std::lock_guard<std::mutex> lk(impl_->mtx);
                impl_->activeStreams[chatId] = ctx;
            }

            auto streamWait = std::make_shared<AsyncWait>();
            rc = lib.ntg_set_stream_sources(impl_->client, chatId, NTG_STREAM_CAPTURE,
                                            ctx->mediaDesc,
                                            streamWait->makeFuture(streamWait));
            if (rc != 0 || !streamWait->wait(15)) {
                log().warning("ntg_set_stream_sources failed for chat " + std::to_string(chatId));
                return PlayResult::ServerError;
            }
            log().info("NTgCalls audio stream active for chat " + std::to_string(chatId) + "!");
        }

        return PlayResult::Ok;
    } catch (const VoiceError& e) {
        return e.category;
    } catch (const std::exception& ex) {
        log().warning(std::string("play error: ") + ex.what());
        return PlayResult::ServerError;
    }
}

bool NtgCallsTransport::pause(std::int64_t chatId) {
    auto& lib = NtgLibrary::instance();
    if (lib.loaded() && impl_->client != 0 && lib.ntg_pause) {
        try {
            auto wait = std::make_shared<AsyncWait>();
            lib.ntg_pause(impl_->client, chatId, wait->makeFuture(wait));
            return wait->wait(5);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool NtgCallsTransport::resume(std::int64_t chatId) {
    auto& lib = NtgLibrary::instance();
    if (lib.loaded() && impl_->client != 0 && lib.ntg_resume) {
        try {
            auto wait = std::make_shared<AsyncWait>();
            lib.ntg_resume(impl_->client, chatId, wait->makeFuture(wait));
            return wait->wait(5);
        } catch (...) {
            return false;
        }
    }
    return true;
}

void NtgCallsTransport::stop(std::int64_t chatId) {
    auto& lib = NtgLibrary::instance();
    if (lib.loaded() && impl_->client != 0 && lib.ntg_stop) {
        try {
            auto wait = std::make_shared<AsyncWait>();
            lib.ntg_stop(impl_->client, chatId, wait->makeFuture(wait));
            wait->wait(5);
        } catch (...) {}
    }
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->activeStreams.erase(chatId);
    }
    try {
        if (impl_->signaling.leaveGroupCall)
            impl_->signaling.leaveGroupCall(chatId);
    } catch (...) {}
}

double NtgCallsTransport::ping() const { return 0.0; }

void NtgCallsTransport::setStreamEndHandler(StreamEndHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->onStreamEnd = std::move(handler);
}

void NtgCallsTransport::setCallClosedHandler(CallClosedHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->onCallClosed = std::move(handler);
}

} // namespace senpai
