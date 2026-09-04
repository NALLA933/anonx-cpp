#ifndef SENPAI_TD_CLIENT_HPP
#define SENPAI_TD_CLIENT_HPP

#include <cstdint>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace senpai {

class TdClient {
public:
    using UpdateHandler = std::function<void(const std::string& updateJson)>;

    TdClient();
    ~TdClient();

    TdClient(const TdClient&) = delete;
    TdClient& operator=(const TdClient&) = delete;

    int clientId() const { return clientId_; }

    void setUpdateHandler(UpdateHandler handler);

    std::string invoke(const std::string& requestJson, int timeoutMs = 30000);

    void send(const std::string& requestJson);

    static std::string execute(const std::string& requestJson);

    static void stopRuntime();

    void onIncoming(std::string json);

private:
    int clientId_ = 0;

    std::mutex pendingMutex_;
    std::unordered_map<std::string, std::shared_ptr<std::promise<std::string>>> pending_;
    std::atomic<std::uint64_t> extraSeq_{0};

    std::mutex handlerMutex_;
    UpdateHandler handler_;
    std::vector<std::string> updateBuffer_;
};

}

#endif
