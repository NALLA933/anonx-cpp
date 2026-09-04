#include <anonx/telegram/tdlib_client.hpp>
#include <anonx/core/logger.hpp>
#include <td/telegram/td_json_client.h>
#include <chrono>
#include <iostream>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace anonx::telegram {

namespace {

typedef int (*fn_td_create_client_id)();
typedef void (*fn_td_send)(int, const char*);
typedef const char* (*fn_td_receive)(double);
typedef const char* (*fn_td_execute)(const char*);

struct DynamicTdLib {
    void* handle{nullptr};
    fn_td_create_client_id create_client_id{nullptr};
    fn_td_send send{nullptr};
    fn_td_receive receive{nullptr};
    fn_td_execute execute{nullptr};

    bool is_loaded() const {
        return (create_client_id != nullptr && send != nullptr && receive != nullptr);
    }

    void load() {
#if defined(SENPAI_WITH_TDLIB) || defined(ANONX_WITH_TDLIB)
        create_client_id = &td_create_client_id;
        send = &td_send;
        receive = &td_receive;
        execute = &td_execute;
        ANONX_LOG_INFO("TDLib", "Using directly linked TDLib symbols.");
        return;
#endif

#if !defined(_WIN32)
        const char* candidate_paths[] = {
            "libtdjson.so",
            "./lib/libtdjson.so",
            "/usr/local/lib/libtdjson.so",
            "/usr/lib/libtdjson.so",
            "/usr/lib/x86_64-linux-gnu/libtdjson.so",
            "/usr/lib/aarch64-linux-gnu/libtdjson.so",
            nullptr
        };

        for (int i = 0; candidate_paths[i] != nullptr; ++i) {
            handle = dlopen(candidate_paths[i], RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                ANONX_LOG_INFO("TDLib", "Dynamically loaded libtdjson from: ", candidate_paths[i]);
                break;
            }
        }

        if (handle) {
            create_client_id = reinterpret_cast<fn_td_create_client_id>(dlsym(handle, "td_create_client_id"));
            send = reinterpret_cast<fn_td_send>(dlsym(handle, "td_send"));
            receive = reinterpret_cast<fn_td_receive>(dlsym(handle, "td_receive"));
            execute = reinterpret_cast<fn_td_execute>(dlsym(handle, "td_execute"));
        } else {
            ANONX_LOG_WARN("TDLib", "libtdjson not found on system; running in simulated Telegram engine mode.");
        }
#endif
    }
};

DynamicTdLib& get_tdlib() {
    static DynamicTdLib lib;
    static std::once_flag flag;
    std::call_once(flag, []() { lib.load(); });
    return lib;
}

} // namespace

TDLibClient::TDLibClient(std::string session_name, bool is_bot)
    : session_name_(std::move(session_name)), is_bot_(is_bot) {}

TDLibClient::~TDLibClient() {
    stop();
}

bool TDLibClient::start(int32_t api_id,
                        const std::string& api_hash,
                        const std::string& token_or_phone,
                        const std::string& data_dir) {
    if (is_running_) {
        return true;
    }

    api_id_ = api_id;
    api_hash_ = api_hash;
    token_or_phone_ = token_or_phone;
    data_dir_ = data_dir;

    auto& td = get_tdlib();
    if (td.is_loaded()) {
        client_id_ = td.create_client_id();
        ANONX_LOG_INFO("TDLib", "Created TDLib client instance (ID: ", client_id_, ", Session: ", session_name_, ")");
    } else {
        client_id_ = 1;
        auth_state_ = AuthState::Ready;
    }

    is_running_ = true;
    receiver_thread_ = std::thread(&TDLibClient::receiver_loop, this);

    // Set log verbosity level
    nlohmann::json set_log_level = {
        {"@type", "setLogVerbosityLevel"},
        {"new_verbosity_level", 1}
    };
    send_request(set_log_level);

    return true;
}

void TDLibClient::stop() {
    if (!is_running_.exchange(false)) {
        return;
    }

    if (client_id_ > 0) {
        nlohmann::json close_req = {{"@type", "close"}};
        send_request(close_req);
    }

    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }

    ANONX_LOG_INFO("TDLib", "Client session '", session_name_, "' stopped cleanly.");
}

void TDLibClient::send_request(const nlohmann::json& request, ResponseCallback on_response) {
    std::string req_id = std::to_string(current_request_id_++);
    nlohmann::json req = request;
    req["@extra"] = req_id;

    if (on_response) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        pending_callbacks_[req_id] = std::move(on_response);
    }

    auto& td = get_tdlib();
    if (td.is_loaded() && client_id_ > 0) {
        std::string serialized = req.dump();
        td.send(client_id_, serialized.c_str());
    }
}

nlohmann::json TDLibClient::execute_sync(const nlohmann::json& request, double timeout_sec) {
    std::string req_id = std::to_string(current_request_id_++);
    nlohmann::json req = request;
    req["@extra"] = req_id;

    auto prom = std::make_shared<std::promise<nlohmann::json>>();
    std::future<nlohmann::json> fut = prom->get_future();

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        sync_promises_[req_id] = prom;
    }

    auto& td = get_tdlib();
    if (td.is_loaded() && client_id_ > 0) {
        std::string serialized = req.dump();
        td.send(client_id_, serialized.c_str());
    } else {
        return {{"@type", "ok"}};
    }

    if (fut.wait_for(std::chrono::duration<double>(timeout_sec)) == std::future_status::ready) {
        return fut.get();
    }

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        sync_promises_.erase(req_id);
    }

    return {{"@type", "error"}, {"code", 408}, {"message", "Request Timeout"}};
}

void TDLibClient::send_message(int64_t chat_id,
                              const std::string& text,
                              int64_t reply_to_message_id,
                              ResponseCallback on_sent) {
    nlohmann::json formatted_text = {
        {"@type", "formattedText"},
        {"text", text}
    };

    nlohmann::json input_message_content = {
        {"@type", "inputMessageText"},
        {"text", formatted_text},
        {"disable_web_page_preview", true},
        {"clear_draft", true}
    };

    nlohmann::json req = {
        {"@type", "sendMessage"},
        {"chat_id", chat_id},
        {"input_message_content", input_message_content}
    };

    if (reply_to_message_id != 0) {
        req["reply_to"] = {
            {"@type", "inputMessageReplyToMessage"},
            {"message_id", reply_to_message_id}
        };
    }

    send_request(req, std::move(on_sent));
}

void TDLibClient::add_update_listener(const std::string& update_type, UpdateCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    update_listeners_[update_type].push_back(std::move(callback));
}

void TDLibClient::set_raw_update_listener(UpdateCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    raw_update_listener_ = std::move(callback);
}

AuthState TDLibClient::auth_state() const noexcept {
    return auth_state_.load();
}

bool TDLibClient::is_ready() const noexcept {
    return auth_state_.load() == AuthState::Ready;
}

int TDLibClient::client_id() const noexcept {
    return client_id_;
}

int64_t TDLibClient::my_id() const noexcept {
    return my_id_;
}

void TDLibClient::receiver_loop() {
    auto& td = get_tdlib();

    while (is_running_) {
        if (!td.is_loaded()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        const char* res = td.receive(1.0);
        if (!res) {
            continue;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(res);

            // 1. Check if it's a response to a tracked request (@extra)
            if (j.contains("@extra")) {
                std::string extra = j["@extra"].get<std::string>();
                ResponseCallback cb = nullptr;
                std::shared_ptr<std::promise<nlohmann::json>> prom = nullptr;

                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    auto it_cb = pending_callbacks_.find(extra);
                    if (it_cb != pending_callbacks_.end()) {
                        cb = std::move(it_cb->second);
                        pending_callbacks_.erase(it_cb);
                    }

                    auto it_prom = sync_promises_.find(extra);
                    if (it_prom != sync_promises_.end()) {
                        prom = it_prom->second;
                        sync_promises_.erase(it_prom);
                    }
                }

                if (cb) cb(j);
                if (prom) prom->set_value(j);
            }

            // 2. Process updates
            process_update(j);
        } catch (const std::exception& ex) {
            ANONX_LOG_ERROR("TDLib", "JSON parsing error in receiver: ", ex.what());
        }
    }
}

void TDLibClient::process_update(const nlohmann::json& update) {
    if (!update.contains("@type")) return;
    std::string type = update["@type"].get<std::string>();

    if (type == "updateAuthorizationState") {
        if (update.contains("authorization_state")) {
            handle_auth_state(update["authorization_state"]);
        }
    } else if (type == "updateUser") {
        if (update.contains("user") && update["user"].value("is_self", false)) {
            my_id_ = update["user"].value("id", int64_t{0});
            ANONX_LOG_INFO("TDLib", "Identified self ID: ", my_id_);
        }
    }

    // Trigger raw listener if present
    UpdateCallback raw_cb;
    std::vector<UpdateCallback> type_cbs;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (raw_update_listener_) raw_cb = raw_update_listener_;
        auto it = update_listeners_.find(type);
        if (it != update_listeners_.end()) {
            type_cbs = it->second;
        }
    }

    if (raw_cb) raw_cb(update);
    for (const auto& cb : type_cbs) {
        cb(update);
    }
}

void TDLibClient::handle_auth_state(const nlohmann::json& state) {
    if (!state.contains("@type")) return;
    std::string type = state["@type"].get<std::string>();

    ANONX_LOG_INFO("TDLib", "Authorization State: ", type);

    if (type == "authorizationStateWaitTdlibParameters") {
        auth_state_ = AuthState::WaitParameters;
        nlohmann::json params = {
            {"@type", "setTdlibParameters"},
            {"database_directory", data_dir_ + "/" + session_name_},
            {"use_message_database", true},
            {"use_secret_chats", false},
            {"api_id", api_id_},
            {"api_hash", api_hash_},
            {"system_language_code", "en"},
            {"device_model", "AnonX Linux Engine"},
            {"system_version", "Debian/Linux"},
            {"application_version", "1.0.0"},
            {"enable_storage_optimizer", true}
        };
        send_request(params);
    } else if (type == "authorizationStateWaitPhoneNumber") {
        if (is_bot_) {
            auth_state_ = AuthState::WaitBotToken;
            nlohmann::json auth_bot = {
                {"@type", "checkAuthenticationBotToken"},
                {"token", token_or_phone_}
            };
            send_request(auth_bot);
        } else {
            auth_state_ = AuthState::WaitPhoneNumber;
            nlohmann::json set_phone = {
                {"@type", "setAuthenticationPhoneNumber"},
                {"phone_number", token_or_phone_}
            };
            send_request(set_phone);
        }
    } else if (type == "authorizationStateWaitCode") {
        auth_state_ = AuthState::WaitCode;
        ANONX_LOG_WARN("TDLib", "Session ", session_name_, " is awaiting Telegram login OTP code.");
    } else if (type == "authorizationStateWaitPassword") {
        auth_state_ = AuthState::WaitPassword;
        ANONX_LOG_WARN("TDLib", "Session ", session_name_, " is awaiting 2FA Cloud Password.");
    } else if (type == "authorizationStateReady") {
        auth_state_ = AuthState::Ready;
        ANONX_LOG_INFO("TDLib", "Session '", session_name_, "' is AUTHORIZED and READY!");
    } else if (type == "authorizationStateLoggingOut") {
        auth_state_ = AuthState::LoggingOut;
    } else if (type == "authorizationStateClosed") {
        auth_state_ = AuthState::Closed;
    }
}

} // namespace anonx::telegram
