#include "senpai/runtime.hpp"

#include <string>
#include <utility>

#include "senpai/logger.hpp"
#include "senpai/plugins_router.hpp"

namespace senpai {
namespace {

Logger log() { return Logger("senpai.runtime"); }

std::string usernameFromLink(const std::string& link) {
    std::string s = link;

    const std::size_t scheme = s.find("//");
    if (scheme != std::string::npos) s = s.substr(scheme + 2);

    const std::size_t slash = s.find('/');
    if (slash != std::string::npos) {

        s = s.substr(slash + 1);
    }
    while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
    if (!s.empty() && s.front() == '@') s.erase(s.begin());

    if (s.empty() || s.front() == '+' || s.find('/') != std::string::npos) return {};
    return s;
}

TelegramClient::Options botOptions(const Config& config,
                                  const Runtime::Options& opts) {
    TelegramClient::Options o;
    o.apiId = static_cast<int>(config.api_id);
    o.apiHash = config.api_hash;
    o.databaseDirectory = opts.botSessionDir;
    o.botToken = config.bot_token;
    o.name = "senpai";
    return o;
}

}

Runtime::Runtime(const Config& config, VoiceTransport& transport, Options opts)
    : config_(config),
      opts_(std::move(opts)),
      db_(config.db_path),
      cache_(),
      lang_(),
      queue_(),
      yt_(),
      sys_(),
      bot_(botOptions(config, opts_)),
      userbot_(static_cast<int>(config.api_id), config.api_hash),
      api_(bot_),
      calls_(transport, queue_, cache_),
      plugins_(Plugins::Deps{api_, db_, cache_, queue_, yt_, calls_, lang_, config_}),
      admin_(AdminPlugins::Deps{api_, db_, cache_, calls_, sys_, lang_, config_}) {}

Runtime::~Runtime() { stop(); }

bool Runtime::start() {
    if (started_.load()) return true;

    log().info(config_.redactedSummary());

    db_.setDefaultLang(config_.lang_code);
    db_.setAssistantCount(config_.assistantCount());

    const int loaded = lang_.loadDir(opts_.localesDir);
    if (loaded <= 0) {
        log().critical("no locale files found in '" + opts_.localesDir +
                       "' — every command would answer with placeholder keys");
        return false;
    }
    if (lang_.loaded(config_.lang_code)) {
        lang_.setDefault(config_.lang_code);
    } else {
        log().warning("LANG_CODE '" + config_.lang_code +
                      "' is not among the loaded locales; falling back to 'en'");
        lang_.setDefault("en");
    }
    log().info("Loaded " + std::to_string(loaded) + " language(s); default '" +
               lang_.defaultCode() + "'");

    if (!bot_.boot()) {
        log().critical("bot account failed to authorize — check BOT_TOKEN");
        return false;
    }
    const TelegramClient::Me& me = bot_.me();
    log().info("Bot authorized: " + me.firstName +
               (me.username.empty() ? "" : " (@" + me.username + ")"));

    if (opts_.bootAssistants) {
        const std::vector<std::string> phones = config_.assistantPhones();
        int assistantSlots = config_.assistantCount();
        if (assistantSlots < 1) assistantSlots = 1;

        if (phones.empty() && !opts_.interactiveAssistantLogin) {
            log().warning("no PHONE_NUMBER* configured and non-interactive login — assistants not booted, "
                          "so voice chats cannot be joined");
        } else {
            for (int i = 0; i < assistantSlots; ++i) {
                Userbot::AssistantSpec spec;
                spec.name = "SenpaiUB" + std::to_string(i + 1);
                if (static_cast<std::size_t>(i) < phones.size()) {
                    spec.phoneNumber = phones[i];
                }
                if (i == 0 && !config_.data_dir.empty()) {
                    spec.sessionDirectory = config_.data_dir;
                } else {
                    std::string sname = (i == 0 && !config_.session_name.empty())
                                            ? config_.session_name
                                            : ("assistant" + std::to_string(i + 1));
                    spec.sessionDirectory = "tdlib/" + sname;
                }
                userbot_.addAssistant(std::move(spec));
            }
            userbot_.setLoggerChatId(config_.logger_id);
            userbot_.setSupportChat(usernameFromLink(config_.support_chat));
            userbot_.setInteractiveLogin(opts_.interactiveAssistantLogin);

            if (!userbot_.bootAll()) {
                log().warning("not every assistant authorized (" +
                              std::to_string(assistantsUp()) + "/" +
                              std::to_string(assistantSlots) + " up)");
            } else {
                log().info(std::to_string(assistantsUp()) + " assistant(s) up");
            }
        }
    }

    dispatcher_ = std::make_unique<Dispatcher>();
    installPlugins(*dispatcher_, plugins_, admin_, db_);
    if (!me.username.empty()) dispatcher_->setBotUsername(me.username);
    dispatcher_->attach(bot_);

    started_.store(true);
    announceStartup();
    log().info("Running.");
    return true;
}

void Runtime::stop() {
    if (stopped_.exchange(true)) return;
    if (!started_.load()) return;

    log().info("Stopping...");
    if (config_.logger_id != 0 && bot_.authorized()) {
        bot_.sendMessage(config_.logger_id, "<b>Bot Stopped</b>");
    }

    bot_.setUpdateObserver(nullptr);
    if (dispatcher_) dispatcher_->stopWorkers();
    userbot_.exitAll();
    bot_.exit();
    TdClient::stopRuntime();
    dispatcher_.reset();

    log().info("Stopped.");
}

std::size_t Runtime::assistantsUp() const {
    std::size_t n = 0;
    for (const std::unique_ptr<TelegramClient>& c : userbot_.clients()) {
        if (c && c->authorized()) ++n;
    }
    return n;
}

void Runtime::announceStartup() {
    if (config_.logger_id == 0) return;

    const TelegramClient::Me& me = bot_.me();
    std::string card = "<b>Bot Started</b>\n\n";
    card += "<b>Bot:</b> " + me.firstName;
    if (!me.username.empty()) card += " | @" + me.username;
    card += "\n<b>Assistants:</b> " + std::to_string(assistantsUp()) + "/" +
            std::to_string(config_.assistantCount());
    card += "\n<b>Languages:</b> " + std::to_string(lang_.codes().size());
    card += "\n<b>Modules:</b> " + std::to_string(AdminPlugins::moduleCount());
    card += "\n<b>Chats served:</b> " + std::to_string(db_.chatCount());

    bot_.sendMessage(config_.logger_id, card);
}

}
