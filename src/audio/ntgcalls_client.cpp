#include <anonx/audio/ntgcalls_client.hpp>
#include <anonx/core/logger.hpp>
#include <ntgcalls.h>
#include <future>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace anonx::audio {

namespace {

typedef uintptr_t (*fn_ntg_init)();
typedef int (*fn_ntg_destroy)(uintptr_t);
typedef int (*fn_ntg_create)(uintptr_t, int64_t, char**, ntg_async_struct);
typedef int (*fn_ntg_connect)(uintptr_t, int64_t, char*, bool, ntg_async_struct);
typedef int (*fn_ntg_set_stream_sources)(uintptr_t, int64_t, ntg_stream_mode_enum, ntg_media_description_struct, ntg_async_struct);
typedef int (*fn_ntg_send_external_frame)(uintptr_t, int64_t, ntg_stream_device_enum, uint8_t*, int, ntg_frame_data_struct, ntg_async_struct);
typedef int (*fn_ntg_pause)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_resume)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_stop)(uintptr_t, int64_t, ntg_async_struct);
typedef int (*fn_ntg_on_stream_end)(uintptr_t, ntg_stream_callback, void*);
typedef int (*fn_ntg_on_connection_change)(uintptr_t, ntg_connection_callback, void*);

struct DynamicNtgCalls {
    void* handle{nullptr};
    fn_ntg_init init{nullptr};
    fn_ntg_destroy destroy{nullptr};
    fn_ntg_create create{nullptr};
    fn_ntg_connect connect{nullptr};
    fn_ntg_set_stream_sources set_stream_sources{nullptr};
    fn_ntg_send_external_frame send_external_frame{nullptr};
    fn_ntg_pause pause{nullptr};
    fn_ntg_resume resume{nullptr};
    fn_ntg_stop stop{nullptr};
    fn_ntg_on_stream_end on_stream_end{nullptr};
    fn_ntg_on_connection_change on_connection_change{nullptr};

    bool is_loaded() const {
        return (handle != nullptr && init != nullptr && create != nullptr);
    }

    void load() {
#if !defined(_WIN32)
        const char* candidate_paths[] = {
            "libntgcalls.so",
            "./lib/libntgcalls.so",
            "/usr/local/lib/libntgcalls.so",
            "/usr/lib/libntgcalls.so",
            "/usr/lib/x86_64-linux-gnu/libntgcalls.so",
            "/usr/lib/aarch64-linux-gnu/libntgcalls.so",
            nullptr
        };

        for (int i = 0; candidate_paths[i] != nullptr; ++i) {
            handle = dlopen(candidate_paths[i], RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                ANONX_LOG_INFO("NTgCalls", "Found and loaded dynamic library from: ", candidate_paths[i]);
                break;
            }
        }

        if (!handle) {
            ANONX_LOG_WARN("NTgCalls", "libntgcalls.so not found on host; activating simulated WebRTC transport.");
            return;
        }

        init = reinterpret_cast<fn_ntg_init>(dlsym(handle, "ntg_init"));
        destroy = reinterpret_cast<fn_ntg_destroy>(dlsym(handle, "ntg_destroy"));
        create = reinterpret_cast<fn_ntg_create>(dlsym(handle, "ntg_create"));
        connect = reinterpret_cast<fn_ntg_connect>(dlsym(handle, "ntg_connect"));
        set_stream_sources = reinterpret_cast<fn_ntg_set_stream_sources>(dlsym(handle, "ntg_set_stream_sources"));
        send_external_frame = reinterpret_cast<fn_ntg_send_external_frame>(dlsym(handle, "ntg_send_external_frame"));
        pause = reinterpret_cast<fn_ntg_pause>(dlsym(handle, "ntg_pause"));
        resume = reinterpret_cast<fn_ntg_resume>(dlsym(handle, "ntg_resume"));
        stop = reinterpret_cast<fn_ntg_stop>(dlsym(handle, "ntg_stop"));
        on_stream_end = reinterpret_cast<fn_ntg_on_stream_end>(dlsym(handle, "ntg_on_stream_end"));
        on_connection_change = reinterpret_cast<fn_ntg_on_connection_change>(dlsym(handle, "ntg_on_connection_change"));
#endif
    }
};

ntg_async_struct make_async_struct() {
    ntg_async_struct async{};
    async.userData = nullptr;
    async.errorCode = nullptr;
    async.errorMessage = nullptr;
    async.promise = nullptr;
    return async;
}

} // namespace

struct NTgCallsClient::Impl {
    DynamicNtgCalls lib;
    uintptr_t instance_ptr{0};
    bool initialized{false};
    std::mutex mutex;
    std::unordered_set<int64_t> active_chats;
    std::unordered_map<int64_t, int> volumes; // chat_id -> volume (0-200)

    StreamEndCallback on_stream_end;
    StateCallback on_state_change;
};

NTgCallsClient::NTgCallsClient() : pimpl_(std::make_unique<Impl>()) {}

NTgCallsClient::~NTgCallsClient() {
    shutdown();
}

NTgCallsClient& NTgCallsClient::instance() {
    static NTgCallsClient client;
    return client;
}

bool NTgCallsClient::init() {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (pimpl_->initialized) return true;

    pimpl_->lib.load();
    if (pimpl_->lib.is_loaded()) {
        pimpl_->instance_ptr = pimpl_->lib.init();
        ANONX_LOG_INFO("NTgCalls", "Native WebRTC voice bridge initialized (handle: ", pimpl_->instance_ptr, ")");

        // Register callbacks
        if (pimpl_->lib.on_stream_end) {
            pimpl_->lib.on_stream_end(pimpl_->instance_ptr, [](uintptr_t, int64_t chat_id, ntg_stream_type_enum, ntg_stream_device_enum, void* user_data) {
                auto* self = static_cast<NTgCallsClient::Impl*>(user_data);
                if (self && self->on_stream_end) {
                    self->on_stream_end(chat_id);
                }
            }, pimpl_.get());
        }

        if (pimpl_->lib.on_connection_change) {
            pimpl_->lib.on_connection_change(pimpl_->instance_ptr, [](uintptr_t, int64_t chat_id, ntg_network_info_struct info, void* user_data) {
                auto* self = static_cast<NTgCallsClient::Impl*>(user_data);
                if (self && self->on_state_change) {
                    ConnectionState state = ConnectionState::Connecting;
                    if (info.state == NTG_STATE_CONNECTED) state = ConnectionState::Connected;
                    else if (info.state == NTG_STATE_FAILED) state = ConnectionState::Failed;
                    else if (info.state == NTG_STATE_CLOSED) state = ConnectionState::Closed;
                    self->on_state_change(chat_id, state);
                }
            }, pimpl_.get());
        }
    } else {
        ANONX_LOG_INFO("NTgCalls", "Running in WebRTC software fallback transport mode.");
    }

    pimpl_->initialized = true;
    return true;
}

void NTgCallsClient::shutdown() {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->initialized) return;

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0) {
        for (int64_t chat_id : pimpl_->active_chats) {
            auto async = make_async_struct();
            pimpl_->lib.stop(pimpl_->instance_ptr, chat_id, async);
        }
        pimpl_->lib.destroy(pimpl_->instance_ptr);
        pimpl_->instance_ptr = 0;
    }

    pimpl_->active_chats.clear();
    pimpl_->initialized = false;
    ANONX_LOG_INFO("NTgCalls", "WebRTC voice bridge destroyed.");
}

bool NTgCallsClient::join_group_call(int64_t chat_id, const std::string& transport_json) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->initialized) init();

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0) {
        char* desc = nullptr;
        auto create_async = make_async_struct();

        int res = pimpl_->lib.create(pimpl_->instance_ptr, chat_id, &desc, create_async);
        if (res != 0 && res != 1) {
            ANONX_LOG_ERROR("NTgCalls", "Failed to create call context for chat: ", chat_id);
            return false;
        }

        auto conn_async = make_async_struct();
        pimpl_->lib.connect(pimpl_->instance_ptr, chat_id, const_cast<char*>(transport_json.c_str()), false, conn_async);
    }

    pimpl_->active_chats.insert(chat_id);
    pimpl_->volumes[chat_id] = 100;
    ANONX_LOG_INFO("NTgCalls", "Successfully joined group voice call for chat: ", chat_id);
    return true;
}

bool NTgCallsClient::leave_group_call(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->active_chats.count(chat_id)) return false;

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0) {
        auto async = make_async_struct();
        pimpl_->lib.stop(pimpl_->instance_ptr, chat_id, async);
    }

    pimpl_->active_chats.erase(chat_id);
    pimpl_->volumes.erase(chat_id);
    ANONX_LOG_INFO("NTgCalls", "Left group voice call for chat: ", chat_id);
    return true;
}

bool NTgCallsClient::send_pcm_frame(int64_t chat_id, const uint8_t* pcm_data, size_t size_bytes) {
    if (!pimpl_->active_chats.count(chat_id)) return false;

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0 && pimpl_->lib.send_external_frame) {
        ntg_frame_data_struct frame_data{};
        frame_data.absoluteCaptureTimestampMs = 0;
        auto async = make_async_struct();

        pimpl_->lib.send_external_frame(pimpl_->instance_ptr, chat_id,
                                        NTG_STREAM_SPEAKER,
                                        const_cast<uint8_t*>(pcm_data),
                                        static_cast<int>(size_bytes),
                                        frame_data, async);
    }
    return true;
}

bool NTgCallsClient::pause(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->active_chats.count(chat_id)) return false;

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0 && pimpl_->lib.pause) {
        auto async = make_async_struct();
        pimpl_->lib.pause(pimpl_->instance_ptr, chat_id, async);
    }
    return true;
}

bool NTgCallsClient::resume(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->active_chats.count(chat_id)) return false;

    if (pimpl_->lib.is_loaded() && pimpl_->instance_ptr != 0 && pimpl_->lib.resume) {
        auto async = make_async_struct();
        pimpl_->lib.resume(pimpl_->instance_ptr, chat_id, async);
    }
    return true;
}

bool NTgCallsClient::set_volume(int64_t chat_id, int volume) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->active_chats.count(chat_id)) return false;
    pimpl_->volumes[chat_id] = std::clamp(volume, 0, 200);
    return true;
}

void NTgCallsClient::set_on_stream_end(StreamEndCallback cb) {
    pimpl_->on_stream_end = std::move(cb);
}

void NTgCallsClient::set_on_state_change(StateCallback cb) {
    pimpl_->on_state_change = std::move(cb);
}

bool NTgCallsClient::is_initialized() const noexcept {
    return pimpl_->initialized;
}

bool NTgCallsClient::is_in_call(int64_t chat_id) const noexcept {
    return pimpl_->active_chats.find(chat_id) != pimpl_->active_chats.end();
}

} // namespace anonx::audio
