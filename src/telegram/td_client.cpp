#include "senpai/telegram/td_client.hpp"

#include <td/telegram/td_json_client.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace senpai {
namespace {

using nlohmann::json;

class TdPump {
public:
    static TdPump& instance() {
        static TdPump p;
        return p;
    }

    void ensureStarted() {
        std::lock_guard<std::mutex> lk(startMutex_);
        if (started_) return;

        td_execute(R"({"@type":"setLogVerbosityLevel","new_verbosity_level":1})");
        stop_.store(false);
        thread_ = std::thread([this] { run(); });
        started_ = true;
    }

    void registerClient(int id, TdClient* c) {
        std::lock_guard<std::mutex> lk(mapMutex_);
        clients_[id] = c;
    }

    void unregisterClient(int id) {
        std::lock_guard<std::mutex> lk(mapMutex_);
        clients_.erase(id);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(startMutex_);
            if (!started_) return;
            stop_.store(true);
        }
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lk(startMutex_);
        started_ = false;
    }

private:
    TdPump() = default;
    ~TdPump() { stop(); }

    void run() {
        while (!stop_.load()) {
            const char* r = td_receive(0.1);
            if (!r) continue;
            std::string s(r);

            json j = json::parse(s, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("@client_id")) continue;

            int cid = 0;
            try {
                cid = j["@client_id"].get<int>();
            } catch (...) {
                continue;
            }

            TdClient* target = nullptr;
            {
                std::lock_guard<std::mutex> lk(mapMutex_);
                auto it = clients_.find(cid);
                if (it != clients_.end()) target = it->second;
            }
            if (target) target->onIncoming(std::move(s));
        }
    }

    std::mutex startMutex_;
    bool started_ = false;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    std::mutex mapMutex_;
    std::unordered_map<int, TdClient*> clients_;
};

}

TdClient::TdClient() {
    static std::once_flag logInitOnce;
    std::call_once(logInitOnce, [] {
        td_execute(R"({"@type":"setLogVerbosityLevel","new_verbosity_level":1})");
    });
    clientId_ = td_create_client_id();
    TdPump::instance().registerClient(clientId_, this);
    TdPump::instance().ensureStarted();

    send(R"({"@type":"getOption","name":"version"})");
}

TdClient::~TdClient() {
    TdPump::instance().unregisterClient(clientId_);

    std::lock_guard<std::mutex> lk(pendingMutex_);
    for (auto& kv : pending_) {
        kv.second->set_value(R"({"@type":"error","code":500,"message":"client destroyed"})");
    }
    pending_.clear();
}

void TdClient::setUpdateHandler(UpdateHandler handler) {
    std::vector<std::string> buffered;
    UpdateHandler h;
    {
        std::lock_guard<std::mutex> lk(handlerMutex_);
        handler_ = std::move(handler);
        h = handler_;
        buffered.swap(updateBuffer_);
    }
    if (h) {
        for (auto& u : buffered) h(u);
    }
}

std::string TdClient::invoke(const std::string& requestJson, int timeoutMs) {
    json j = json::parse(requestJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return R"({"@type":"error","code":400,"message":"invalid request json"})";
    }

    const std::string extra =
        "req" + std::to_string(clientId_) + "_" +
        std::to_string(extraSeq_.fetch_add(1));
    j["@extra"] = extra;

    auto prom = std::make_shared<std::promise<std::string>>();
    std::future<std::string> fut = prom->get_future();
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        pending_[extra] = prom;
    }

    const std::string payload = j.dump();
    td_send(clientId_, payload.c_str());

    if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
        return fut.get();
    }
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        pending_.erase(extra);
    }
    return R"({"@type":"error","code":408,"message":"invoke timeout"})";
}

void TdClient::send(const std::string& requestJson) {
    td_send(clientId_, requestJson.c_str());
}

std::string TdClient::execute(const std::string& requestJson) {
    const char* r = td_execute(requestJson.c_str());
    return r ? std::string(r) : std::string();
}

void TdClient::stopRuntime() {
    TdPump::instance().stop();
}

void TdClient::onIncoming(std::string s) {
    json j = json::parse(s, nullptr, false);

    if (!j.is_discarded() && j.is_object() && j.contains("@extra")) {
        std::string extra;
        try {
            extra = j["@extra"].get<std::string>();
        } catch (...) {
            extra.clear();
        }
        if (!extra.empty()) {
            std::shared_ptr<std::promise<std::string>> prom;
            {
                std::lock_guard<std::mutex> lk(pendingMutex_);
                auto it = pending_.find(extra);
                if (it != pending_.end()) {
                    prom = it->second;
                    pending_.erase(it);
                }
            }
            if (prom) {
                prom->set_value(std::move(s));
            }
        }
        return;
    }

    UpdateHandler h;
    {
        std::lock_guard<std::mutex> lk(handlerMutex_);
        if (handler_) {
            h = handler_;
        } else {
            updateBuffer_.push_back(std::move(s));
            return;
        }
    }
    h(s);
}

}
