#include <anonx/core/config.hpp>
#include <anonx/core/logger.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace anonx::core {

namespace {

std::optional<std::string> get_env_var(const char* name) {
    const char* val = std::getenv(name);
    if (val && *val) {
        return std::string(val);
    }
    return std::nullopt;
}

} // namespace

std::vector<std::string> ConfigLoader::split_string(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream token_stream(s);
    while (std::getline(token_stream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

BotConfig ConfigLoader::load(const std::string& config_path) {
    BotConfig config;

    // 1. Try reading configuration file if present
    std::ifstream file(config_path);
    if (file.is_open()) {
        try {
            nlohmann::json j;
            file >> j;

            if (j.contains("bot_token")) config.bot_token = j["bot_token"].get<std::string>();
            if (j.contains("api_id")) config.api_id = j["api_id"].get<int32_t>();
            if (j.contains("api_hash")) config.api_hash = j["api_hash"].get<std::string>();
            if (j.contains("string_session")) config.string_session = j["string_session"].get<std::string>();
            if (j.contains("owner_id")) config.owner_id = j["owner_id"].get<int64_t>();

            if (j.contains("sudo_users") && j["sudo_users"].is_array()) {
                for (const auto& item : j["sudo_users"]) {
                    config.sudo_users.insert(item.get<int64_t>());
                }
            }

            if (j.contains("mongo_uri")) config.mongo_uri = j["mongo_uri"].get<std::string>();
            if (j.contains("db_name")) config.db_name = j["db_name"].get<std::string>();
            if (j.contains("log_group_id")) config.log_group_id = j["log_group_id"].get<int64_t>();
            if (j.contains("audio_quality")) config.audio_quality = j["audio_quality"].get<std::string>();
            if (j.contains("duration_limit_sec")) config.duration_limit_sec = j["duration_limit_sec"].get<int32_t>();
            if (j.contains("log_level")) config.log_level = j["log_level"].get<std::string>();
            if (j.contains("data_dir")) config.data_dir = j["data_dir"].get<std::string>();
            if (j.contains("downloads_dir")) config.downloads_dir = j["downloads_dir"].get<std::string>();
            if (j.contains("auto_update_ytdlp")) config.auto_update_ytdlp = j["auto_update_ytdlp"].get<bool>();

            ANONX_LOG_INFO("Config", "Loaded configuration file from: ", config_path);
        } catch (const std::exception& ex) {
            ANONX_LOG_WARN("Config", "Failed to parse ", config_path, ": ", ex.what(), " (using defaults & env vars)");
        }
    } else {
        ANONX_LOG_INFO("Config", "Config file '", config_path, "' not found; relying on environment variables.");
    }

    // 2. Override with Environment Variables (Highest Precedence)
    apply_env_overrides(config);

    return config;
}

void ConfigLoader::apply_env_overrides(BotConfig& config) {
    if (auto val = get_env_var("BOT_TOKEN")) config.bot_token = *val;
    if (auto val = get_env_var("API_ID")) {
        try { config.api_id = std::stoi(*val); } catch (...) {}
    }
    if (auto val = get_env_var("API_HASH")) config.api_hash = *val;
    if (auto val = get_env_var("STRING_SESSION")) config.string_session = *val;
    else if (auto val = get_env_var("SESSION_STRING")) config.string_session = *val;

    if (auto val = get_env_var("OWNER_ID")) {
        try { config.owner_id = std::stoll(*val); } catch (...) {}
    }

    if (auto val = get_env_var("SUDO_USERS")) {
        auto tokens = split_string(*val, ' ');
        for (const auto& token : tokens) {
            try {
                config.sudo_users.insert(std::stoll(token));
            } catch (...) {}
        }
    }

    if (auto val = get_env_var("MONGO_URI")) config.mongo_uri = *val;
    else if (auto val = get_env_var("MONGO_DB_URI")) config.mongo_uri = *val;

    if (auto val = get_env_var("DB_NAME")) config.db_name = *val;

    if (auto val = get_env_var("LOG_GROUP_ID")) {
        try { config.log_group_id = std::stoll(*val); } catch (...) {}
    }

    if (auto val = get_env_var("LOG_LEVEL")) config.log_level = *val;
    if (auto val = get_env_var("AUDIO_QUALITY")) config.audio_quality = *val;
    if (auto val = get_env_var("AUTO_UPDATE_YTDLP")) {
        config.auto_update_ytdlp = (*val == "1" || *val == "true" || *val == "TRUE");
    }
}

void ConfigLoader::save(const BotConfig& config, const std::string& config_path) {
    nlohmann::json j;
    j["bot_token"] = config.bot_token;
    j["api_id"] = config.api_id;
    j["api_hash"] = config.api_hash;
    j["string_session"] = config.string_session;
    j["owner_id"] = config.owner_id;
    j["sudo_users"] = std::vector<int64_t>(config.sudo_users.begin(), config.sudo_users.end());
    j["mongo_uri"] = config.mongo_uri;
    j["db_name"] = config.db_name;
    j["log_group_id"] = config.log_group_id;
    j["audio_quality"] = config.audio_quality;
    j["duration_limit_sec"] = config.duration_limit_sec;
    j["log_level"] = config.log_level;
    j["data_dir"] = config.data_dir;
    j["downloads_dir"] = config.downloads_dir;
    j["auto_update_ytdlp"] = config.auto_update_ytdlp;

    std::ofstream file(config_path);
    if (file.is_open()) {
        file << j.dump(4);
    }
}

} // namespace anonx::core
