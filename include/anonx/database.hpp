#pragma once

#include <cstdint>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace anonx {

class DatabaseError : public std::runtime_error {
public:
    explicit DatabaseError(const std::string& msg) : std::runtime_error(msg) {}
};

class Database {
public:

    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&)                 = delete;
    Database& operator=(Database&&)      = delete;

    void setDefaultLang(const std::string& code);

    void setAssistantCount(int count);

    bool                     isChat(std::int64_t chatId);
    bool                     addChat(std::int64_t chatId);
    bool                     removeChat(std::int64_t chatId);
    std::vector<std::int64_t> getChats();
    std::size_t              chatCount();

    bool                     isUser(std::int64_t userId);
    bool                     addUser(std::int64_t userId);
    bool                     removeUser(std::int64_t userId);
    std::vector<std::int64_t> getUsers();
    std::size_t              userCount();

    bool                     isAuth(std::int64_t chatId, std::int64_t userId);
    bool                     addAuth(std::int64_t chatId, std::int64_t userId);
    bool                     removeAuth(std::int64_t chatId, std::int64_t userId);
    std::vector<std::int64_t> getAuthUsers(std::int64_t chatId);

    std::string getLang(std::int64_t chatId);
    bool        setLang(std::int64_t chatId, const std::string& langCode);

    int getAssistant(std::int64_t chatId);
    int setAssistant(std::int64_t chatId);

    bool                     isBlacklistedChat(std::int64_t chatId);
    bool                     addBlacklistChat(std::int64_t chatId);
    bool                     removeBlacklistChat(std::int64_t chatId);
    std::vector<std::int64_t> getBlacklistedChats();

    bool                     isBlacklistedUser(std::int64_t userId);
    bool                     addBlacklistUser(std::int64_t userId);
    bool                     removeBlacklistUser(std::int64_t userId);
    std::vector<std::int64_t> getBlacklistedUsers();

    bool addBlacklist(std::int64_t id);
    bool removeBlacklist(std::int64_t id);

    bool                     isSudo(std::int64_t userId);
    bool                     addSudo(std::int64_t userId);
    bool                     removeSudo(std::int64_t userId);
    std::vector<std::int64_t> getSudoers();

    bool getCmdDelete(std::int64_t chatId);
    bool setCmdDelete(std::int64_t chatId, bool enabled);

    bool getPlayMode(std::int64_t chatId);
    bool setPlayMode(std::int64_t chatId, bool enabled);

    std::string getSetting(const std::string& key, const std::string& fallback = "");
    bool        setSetting(const std::string& key, const std::string& value);

    bool getLoggerEnabled();
    bool setLoggerEnabled(bool enabled);

private:

    void execOrThrow(const char* sql);
    void loadIdSet(const char* sql, std::unordered_set<std::int64_t>& out);
    bool insertId(const char* sql, std::int64_t id);
    bool deleteId(const char* sql, std::int64_t id);

    void ensureChatsLoaded();
    void ensureUsersLoaded();
    void ensureAuthLoaded(std::int64_t chatId);
    void ensureBlChatsLoaded();
    void ensureBlUsersLoaded();
    void ensureSudoLoaded();

    int  assignAssistantLocked(std::int64_t chatId);
    bool getChatFlagLocked(std::int64_t chatId, const char* selectSql,
                           std::unordered_map<std::int64_t, bool>& cache);
    bool setChatFlagLocked(std::int64_t chatId, bool enabled, const char* upsertSql,
                           std::unordered_map<std::int64_t, bool>& cache);

    sqlite3*   db_ = nullptr;
    std::mutex mtx_;

    std::unordered_set<std::int64_t> chats_;    bool chatsLoaded_   = false;
    std::unordered_set<std::int64_t> users_;    bool usersLoaded_   = false;

    std::unordered_map<std::int64_t, std::unordered_set<std::int64_t>> auth_;
    std::unordered_map<std::int64_t, std::string> lang_;
    std::unordered_map<std::int64_t, int>         assistant_;
    std::unordered_set<std::int64_t> blChats_;   bool blChatsLoaded_ = false;
    std::unordered_set<std::int64_t> blUsers_;   bool blUsersLoaded_ = false;
    std::unordered_set<std::int64_t> sudoers_;   bool sudoLoaded_    = false;
    std::unordered_map<std::int64_t, bool> cmdDelete_;
    std::unordered_map<std::int64_t, bool> adminPlay_;
    std::unordered_map<std::string, std::string> settings_;

    std::string  defaultLang_    = "en";
    int          assistantCount_ = 1;
    std::mt19937 rng_;
};

}
