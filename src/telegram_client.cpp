// AnonXMusic C++ port — Phase 4
// telegram_client.cpp — authorization state machine + high-level operations.

#include "anonx/telegram_client.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

#include "anonx/logger.hpp"

namespace anonx {
namespace {

using nlohmann::json;

Logger log() { return Logger("anonx.telegram"); }

// Safe string-field read: returns "" unless the key holds a string.
std::string strField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return std::string();
}

// Safe int64 read from a numeric field. With real nlohmann this keeps the full
// 64-bit range (ids can exceed 2^53), as numbers are stored as int64 not double.
std::int64_t intField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_number()) {
        return j[key].get<std::int64_t>();
    }
    return 0;
}

TelegramClient::Me parseUser(const json& u) {
    TelegramClient::Me m;
    m.id = intField(u, "id");
    m.firstName = strField(u, "first_name");

    // TDLib >=1.8 exposes usernames.active_usernames[]; older builds use username.
    if (u.contains("usernames") && u["usernames"].is_object()) {
        const json& un = u["usernames"];
        if (un.contains("active_usernames") && un["active_usernames"].is_array() &&
            un["active_usernames"].size() > 0 && un["active_usernames"][0].is_string()) {
            m.username = un["active_usernames"][0].get<std::string>();
        }
    } else {
        m.username = strField(u, "username");
    }

    const std::string label = m.firstName.empty() ? "user" : m.firstName;
    m.mention = "<a href=\"tg://user?id=" + std::to_string(m.id) + "\">" + label + "</a>";
    return m;
}

// --- helpers added for the integration phase -----------------------------

// Standard base64. TDLib's JSON interface transports `bytes` fields base64
// encoded, and inlineKeyboardButtonTypeCallback.data is such a field — so a
// button payload must be encoded on the way out. The Dispatcher already does the
// mirror-image decode on the way in (base64Decode in dispatcher.cpp); if only
// one side is done the buttons silently stop matching their handlers.
std::string base64Encode(const std::string& in) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.append("==");
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// Serialize an InlineKeyboard into TDLib's replyMarkupInlineKeyboard. Returns a
// null json when the keyboard is empty, which is how "no markup" is expressed.
json toReplyMarkup(const InlineKeyboard& kb) {
    if (kb.empty()) return json();

    json rows = json::array();
    for (const auto& row : kb) {
        json cells = json::array();
        for (const auto& b : row) {
            json type;
            switch (b.kind) {
                case InlineButton::Kind::Url:
                    type["@type"] = "inlineKeyboardButtonTypeUrl";
                    type["url"] = b.url;
                    break;
                case InlineButton::Kind::Copy:
                    // TDLib >= 1.8.36. Older builds reject the @type; such a
                    // button is cosmetic (it only offers a copy shortcut), so a
                    // failed edit degrades to no keyboard rather than no message.
                    type["@type"] = "inlineKeyboardButtonTypeCopyText";
                    type["text"] = b.copy;
                    break;
                case InlineButton::Kind::Callback:
                default:
                    type["@type"] = "inlineKeyboardButtonTypeCallback";
                    type["data"] = base64Encode(b.data);
                    break;
            }
            json cell;
            cell["@type"] = "inlineKeyboardButton";
            cell["text"] = b.text;
            cell["type"] = type;
            cells.push_back(cell);
        }
        rows.push_back(cells);
    }

    json markup;
    markup["@type"] = "replyMarkupInlineKeyboard";
    markup["rows"] = rows;
    return markup;
}

// Turn HTML into a TDLib formattedText using the library's own synchronous
// parser, mirroring Pyrogram's ParseMode.HTML. Falls back to unformatted text so
// a malformed tag can never swallow the message.
json parseHtml(const std::string& html) {
    json req;
    req["@type"] = "parseTextEntities";
    req["text"] = html;
    json mode;
    mode["@type"] = "textParseModeHTML";
    req["parse_mode"] = mode;

    json formatted = json::parse(TdClient::execute(req.dump()), nullptr, false);
    if (!formatted.is_discarded() && formatted.is_object() &&
        strField(formatted, "@type") == "formattedText") {
        return formatted;
    }
    json plain;
    plain["@type"] = "formattedText";
    plain["text"] = html;
    return plain;
}

json inputMessageText(const std::string& html) {
    json content;
    content["@type"] = "inputMessageText";
    content["text"] = parseHtml(html);
    return content;
}

std::string htmlEscapeText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out.push_back(c); break;
        }
    }
    return out;
}

// Opening/closing HTML tags for a TDLib text-entity type. Returns false for
// entity types with no HTML representation (mentions, hashtags, bot commands and
// the like are plain text as far as re-rendering goes).
bool entityTags(const json& type, std::string& open, std::string& close) {
    const std::string t = strField(type, "@type");
    if (t == "textEntityTypeBold")          { open = "<b>";  close = "</b>";  return true; }
    if (t == "textEntityTypeItalic")        { open = "<i>";  close = "</i>";  return true; }
    if (t == "textEntityTypeUnderline")     { open = "<u>";  close = "</u>";  return true; }
    if (t == "textEntityTypeStrikethrough") { open = "<s>";  close = "</s>";  return true; }
    if (t == "textEntityTypeCode")          { open = "<code>"; close = "</code>"; return true; }
    if (t == "textEntityTypePre")           { open = "<pre>";  close = "</pre>";  return true; }
    if (t == "textEntityTypeSpoiler") {
        open = "<span class=\"tg-spoiler\">"; close = "</span>"; return true;
    }
    if (t == "textEntityTypeTextUrl") {
        open = "<a href=\"" + htmlEscapeText(strField(type, "url")) + "\">";
        close = "</a>";
        return true;
    }
    if (t == "textEntityTypeMentionName") {
        open = "<a href=\"tg://user?id=" + std::to_string(intField(type, "user_id")) + "\">";
        close = "</a>";
        return true;
    }
    return false;
}

// Re-render a TDLib formattedText as HTML.
std::string formattedTextToHtml(const json& formatted) {
    const std::string text = strField(formatted, "text");
    if (text.empty()) return std::string();

    struct Span { int start; int end; std::string open; std::string close; };
    std::vector<Span> spans;
    if (formatted.contains("entities") && formatted["entities"].is_array()) {
        for (const auto& e : formatted["entities"]) {
            if (!e.is_object() || !e.contains("type") || !e["type"].is_object()) continue;
            std::string open, close;
            if (!entityTags(e["type"], open, close)) continue;
            const int off = static_cast<int>(intField(e, "offset"));
            const int len = static_cast<int>(intField(e, "length"));
            if (len <= 0) continue;
            spans.push_back(Span{off, off + len, open, close});
        }
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end > b.end;
    });

    std::string out;
    std::vector<std::pair<int, std::string>> openSpans;  // end position, close tag
    std::size_t nextSpan = 0;
    int utf16 = 0;

    auto flushAt = [&](int pos) {
        while (!openSpans.empty() && openSpans.back().first <= pos) {
            out += openSpans.back().second;
            openSpans.pop_back();
        }
        while (nextSpan < spans.size() && spans[nextSpan].start == pos) {
            out += spans[nextSpan].open;
            openSpans.emplace_back(spans[nextSpan].end, spans[nextSpan].close);
            ++nextSpan;
        }
    };

    for (std::size_t i = 0; i < text.size();) {
        flushAt(utf16);

        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t bytes = 1;
        int units = 1;
        if (c >= 0xF0)      { bytes = 4; units = 2; }   // outside the BMP: surrogate pair
        else if (c >= 0xE0) { bytes = 3; }
        else if (c >= 0xC0) { bytes = 2; }
        if (i + bytes > text.size()) bytes = text.size() - i;

        const std::string ch = text.substr(i, bytes);
        if (bytes == 1) {
            out += htmlEscapeText(ch);
        } else {
            out += ch;
        }
        i += bytes;
        utf16 += units;
    }
    flushAt(utf16);
    while (!openSpans.empty()) {
        out += openSpans.back().second;
        openSpans.pop_back();
    }
    return out;
}

// Find the text/caption of a message object, whatever content type it carries.
const json* messageFormattedText(const json& message) {
    if (!message.is_object() || !message.contains("content") ||
        !message["content"].is_object()) {
        return nullptr;
    }
    const json& content = message["content"];
    for (const char* key : {"text", "caption"}) {
        if (content.contains(key) && content[key].is_object() &&
            strField(content[key], "@type") == "formattedText") {
            return &content[key];
        }
    }
    return nullptr;
}

bool isOk(const std::string& responseJson) {
    json j = json::parse(responseJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    const std::string t = strField(j, "@type");
    return t != "error" && !t.empty();
}

}  // namespace

TelegramClient::TelegramClient(Options opts) : opts_(std::move(opts)) {}

TelegramClient::~TelegramClient() {
    exit();
}

void TelegramClient::setStatus(Status s) {
    {
        std::lock_guard<std::mutex> lk(cvMutex_);
        status_.store(s);
    }
    cv_.notify_all();
}

void TelegramClient::setUpdateObserver(TdClient::UpdateHandler observer) {
    std::lock_guard<std::mutex> lk(observerMutex_);
    observer_ = std::move(observer);
}

void TelegramClient::onUpdate(const std::string& updateJson) {
    json j = json::parse(updateJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;

    const std::string type = strField(j, "@type");

    // Locate an authorization-state object, whether delivered as an
    // updateAuthorizationState or as a bare authorizationState* response.
    const json* authState = nullptr;
    if (type == "updateAuthorizationState" && j.contains("authorization_state")) {
        authState = &j["authorization_state"];
    } else if (type.rfind("authorizationState", 0) == 0) {
        authState = &j;
    }

    if (type == "error") {
        log().error(opts_.name + " TDLib error: " + strField(j, "message") +
                    " (code " + std::to_string(intField(j, "code")) + ")");
    }

    if (!authState) {
        // Not an auth message — hand it to the observer (Dispatcher etc.).
        TdClient::UpdateHandler obs;
        {
            std::lock_guard<std::mutex> lk(observerMutex_);
            obs = observer_;
        }
        if (obs) obs(updateJson);
        return;
    }

    const std::string st = strField(*authState, "@type");

    if (st == "authorizationStateWaitTdlibParameters") {
        json p;
        p["@type"] = "setTdlibParameters";
        p["use_test_dc"] = false;
        p["database_directory"] = opts_.databaseDirectory;
        p["files_directory"] = "";
        p["database_encryption_key"] = "";
        p["use_file_database"] = true;
        p["use_chat_info_database"] = true;
        p["use_message_database"] = false;   // lightweight: no history on disk
        p["use_secret_chats"] = false;
        p["api_id"] = opts_.apiId;
        p["api_hash"] = opts_.apiHash;
        p["system_language_code"] = opts_.systemLanguageCode;
        p["device_model"] = opts_.deviceModel;
        p["system_version"] = "Linux";
        p["application_version"] = opts_.applicationVersion;
        client_.send(p.dump());

    } else if (st == "authorizationStateWaitEncryptionKey") {
        // Only present on older TDLib versions.
        json p;
        p["@type"] = "checkDatabaseEncryptionKey";
        p["encryption_key"] = "";
        client_.send(p.dump());

    } else if (st == "authorizationStateWaitPhoneNumber") {
        if (!opts_.botToken.empty()) {
            json p;
            p["@type"] = "checkAuthenticationBotToken";
            p["token"] = opts_.botToken;
            client_.send(p.dump());
        } else {
            std::string phone = opts_.phoneNumber;
            if (phone.empty() && opts_.phoneProvider) {
                phone = opts_.phoneProvider();
            }
            if (phone.empty()) {
                std::fprintf(stderr, "\nEnter phone number for %s (e.g. +1234567890): ", opts_.name.c_str());
                std::fflush(stderr);
                std::getline(std::cin, phone);
            }
            // Trim trailing and leading whitespace
            while (!phone.empty() && (phone.back() == '\r' || phone.back() == '\n' || phone.back() == ' ')) phone.pop_back();
            std::size_t s = 0;
            while (s < phone.size() && (phone[s] == ' ' || phone[s] == '\t')) ++s;
            phone = phone.substr(s);

            if (!phone.empty()) {
                opts_.phoneNumber = phone;
                log().info(opts_.name + ": Requesting Telegram login code for " + phone + "...");
                json p;
                p["@type"] = "setAuthenticationPhoneNumber";
                p["phone_number"] = phone;
                client_.send(p.dump());
            } else {
                log().error(opts_.name + ": no bot token or phone number configured");
                setStatus(Status::Error);
            }
        }

    } else if (st == "authorizationStateWaitCode") {
        // First-run interactive login for a userbot.
        if (authState->contains("code_info") && (*authState)["code_info"].is_object()) {
            const json& ci = (*authState)["code_info"];
            std::string deliveryType = strField(ci["type"], "@type");
            if (deliveryType == "authenticationCodeTypeTelegramMessage") {
                log().info(opts_.name + ": >>> Code sent via official Telegram app message! Please check Telegram. <<<");
            } else if (deliveryType == "authenticationCodeTypeSms") {
                log().info(opts_.name + ": >>> Code sent via SMS to " + opts_.phoneNumber + "! <<<");
            } else if (deliveryType == "authenticationCodeTypeCall") {
                log().info(opts_.name + ": >>> Telegram will call your phone with the login code! <<<");
            } else {
                log().info(opts_.name + ": >>> Telegram code was dispatched. Please check your Telegram app / SMS. <<<");
            }
        }

        std::string code;
        if (opts_.codeProvider) {
            code = opts_.codeProvider();
        } else {
            std::fprintf(stderr, "\nEnter Telegram login code for %s: ", opts_.name.c_str());
            std::fflush(stderr);
            std::getline(std::cin, code);
        }
        while (!code.empty() && (code.back() == '\r' || code.back() == '\n' || code.back() == ' ')) code.pop_back();
        std::size_t s = 0;
        while (s < code.size() && (code[s] == ' ' || code[s] == '\t')) ++s;
        code = code.substr(s);

        log().info(opts_.name + ": Submitting authentication code...");
        json p;
        p["@type"] = "checkAuthenticationCode";
        p["code"] = code;
        client_.send(p.dump());

    } else if (st == "authorizationStateWaitPassword") {
        log().info(opts_.name + ": 2-Step Verification (2FA password) is enabled on this Telegram account.");
        std::string pw;
        if (opts_.passwordProvider) {
            pw = opts_.passwordProvider();
        } else {
            std::fprintf(stderr, "\nEnter 2FA cloud password for %s: ", opts_.name.c_str());
            std::fflush(stderr);
            std::getline(std::cin, pw);
        }
        while (!pw.empty() && (pw.back() == '\r' || pw.back() == '\n')) pw.pop_back();

        log().info(opts_.name + ": Submitting 2FA password...");
        json p;
        p["@type"] = "checkAuthenticationPassword";
        p["password"] = pw;
        client_.send(p.dump());

    } else if (st == "authorizationStateWaitRegistration") {
        log().error(opts_.name + ": account is unregistered; refusing to auto-register");
        setStatus(Status::Error);

    } else if (st == "authorizationStateReady") {
        log().info(opts_.name + ": authorized and session persisted to " + opts_.databaseDirectory);
        setStatus(Status::Ready);

    } else if (st == "authorizationStateClosed") {
        setStatus(Status::Closed);
    }
    // authorizationStateClosing / LoggingOut: transient, wait for Closed.
}

bool TelegramClient::boot(int timeoutMs) {
    // Installing the handler flushes any authorization update TDLib already
    // emitted, so the state machine starts immediately.
    client_.setUpdateHandler([this](const std::string& u) { onUpdate(u); });

    {
        std::unique_lock<std::mutex> lk(cvMutex_);
        const bool signalled = cv_.wait_for(
            lk, std::chrono::milliseconds(timeoutMs), [this] {
                const Status s = status_.load();
                return s == Status::Ready || s == Status::Error || s == Status::Closed;
            });
        if (!signalled) {
            log().warning(opts_.name + ": authorization timed out");
            return false;
        }
    }

    if (status_.load() != Status::Ready) {
        log().error(opts_.name + ": authorization failed");
        return false;
    }

    getMe();
    if (me_.username.empty()) {
        log().info(opts_.name + " started as " + me_.firstName +
                   " (id " + std::to_string(me_.id) + ")");
    } else {
        log().info(opts_.name + " started as @" + me_.username +
                   " (id " + std::to_string(me_.id) + ")");
    }
    return true;
}

void TelegramClient::exit() {
    if (closeRequested_.exchange(true)) return;
    client_.send(R"({"@type":"close"})");
    std::unique_lock<std::mutex> lk(cvMutex_);
    cv_.wait_for(lk, std::chrono::milliseconds(5000),
                 [this] { return status_.load() == Status::Closed; });
}

TelegramClient::Me TelegramClient::getMe() {
    const std::string resp = client_.invoke(R"({"@type":"getMe"})");
    json j = json::parse(resp, nullptr, false);
    if (!j.is_discarded() && j.is_object() && strField(j, "@type") == "user") {
        me_ = parseUser(j);
    }
    return me_;
}

std::int64_t TelegramClient::sendMessage(std::int64_t chatId, const std::string& html,
                                         const InlineKeyboard& kb) {
    json req;
    req["@type"] = "sendMessage";
    req["chat_id"] = chatId;
    req["input_message_content"] = inputMessageText(html);
    const json markup = toReplyMarkup(kb);
    if (!markup.is_null()) req["reply_markup"] = markup;

    const std::string resp = client_.invoke(req.dump());
    json j = json::parse(resp, nullptr, false);
    if (!j.is_discarded() && j.is_object() && strField(j, "@type") == "message") {
        return intField(j, "id");
    }
    return 0;
}

bool TelegramClient::editMessageText(std::int64_t chatId, std::int64_t messageId,
                                     const std::string& html, const InlineKeyboard& kb) {
    json req;
    req["@type"] = "editMessageText";
    req["chat_id"] = chatId;
    req["message_id"] = messageId;
    req["input_message_content"] = inputMessageText(html);
    const json markup = toReplyMarkup(kb);
    if (!markup.is_null()) req["reply_markup"] = markup;
    return isOk(client_.invoke(req.dump()));
}

bool TelegramClient::editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                            const InlineKeyboard& kb) {
    json req;
    req["@type"] = "editMessageReplyMarkup";
    req["chat_id"] = chatId;
    req["message_id"] = messageId;
    const json markup = toReplyMarkup(kb);
    if (!markup.is_null()) req["reply_markup"] = markup;
    return isOk(client_.invoke(req.dump()));
}

bool TelegramClient::deleteMessages(std::int64_t chatId,
                                    const std::vector<std::int64_t>& messageIds,
                                    bool revoke) {
    if (messageIds.empty()) return true;
    json ids = json::array();
    for (std::int64_t id : messageIds) ids.push_back(id);

    json req;
    req["@type"] = "deleteMessages";
    req["chat_id"] = chatId;
    req["message_ids"] = ids;
    req["revoke"] = revoke;
    return isOk(client_.invoke(req.dump()));
}

std::string TelegramClient::getMessageText(std::int64_t chatId, std::int64_t messageId) {
    json req;
    req["@type"] = "getMessage";
    req["chat_id"] = chatId;
    req["message_id"] = messageId;

    json j = json::parse(client_.invoke(req.dump()), nullptr, false);
    if (j.is_discarded() || !j.is_object() || strField(j, "@type") != "message") {
        return std::string();
    }
    const json* ft = messageFormattedText(j);
    return ft ? formattedTextToHtml(*ft) : std::string();
}

std::int64_t TelegramClient::getMessageSenderId(std::int64_t chatId,
                                                std::int64_t messageId) {
    json req;
    req["@type"] = "getMessage";
    req["chat_id"] = chatId;
    req["message_id"] = messageId;

    json j = json::parse(client_.invoke(req.dump()), nullptr, false);
    if (j.is_discarded() || !j.is_object() || strField(j, "@type") != "message") {
        return 0;
    }
    if (j.contains("sender_id") && j["sender_id"].is_object() &&
        strField(j["sender_id"], "@type") == "messageSenderUser") {
        return intField(j["sender_id"], "user_id");
    }
    return 0;
}

bool TelegramClient::forwardMessages(std::int64_t fromChatId,
                                     const std::vector<std::int64_t>& messageIds,
                                     std::int64_t toChatId, bool sendCopy) {
    if (messageIds.empty()) return true;
    json ids = json::array();
    for (std::int64_t id : messageIds) ids.push_back(id);

    json req;
    req["@type"] = "forwardMessages";
    req["chat_id"] = toChatId;          // destination
    req["from_chat_id"] = fromChatId;   // source
    req["message_ids"] = ids;
    req["send_copy"] = sendCopy;        // true == message.copy() (no "forwarded from")
    req["remove_caption"] = false;

    json j = json::parse(client_.invoke(req.dump()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (strField(j, "@type") == "error") return false;
    if (strField(j, "@type") == "messages") return intField(j, "total_count") > 0;
    return true;
}

void TelegramClient::answerCallbackQuery(std::int64_t queryId, const std::string& text,
                                         bool alert) {
    json req;
    req["@type"] = "answerCallbackQuery";
    req["callback_query_id"] = queryId;
    if (!text.empty()) req["text"] = text;
    req["show_alert"] = alert;
    client_.send(req.dump());
}

void TelegramClient::leaveChat(std::int64_t chatId) {
    json req;
    req["@type"] = "leaveChat";
    req["chat_id"] = chatId;
    client_.send(req.dump());
}

std::string TelegramClient::chatTitle(std::int64_t chatId) {
    json req;
    req["@type"] = "getChat";
    req["chat_id"] = chatId;
    json j = json::parse(client_.invoke(req.dump()), nullptr, false);
    if (!j.is_discarded() && j.is_object() && strField(j, "@type") == "chat") {
        return strField(j, "title");
    }
    return std::string();
}

TelegramClient::UserInfo TelegramClient::getUser(std::int64_t userId) {
    UserInfo info;
    info.id = userId;

    json req;
    req["@type"] = "getUser";
    req["user_id"] = userId;
    json j = json::parse(client_.invoke(req.dump()), nullptr, false);
    if (j.is_discarded() || !j.is_object() || strField(j, "@type") != "user") {
        return info;
    }
    const Me m = parseUser(j);
    info.firstName = m.firstName;
    info.username = m.username;
    info.found = true;
    return info;
}

std::string TelegramClient::messageLink(std::int64_t chatId, std::int64_t messageId) {
    {
        json req;
        req["@type"] = "getMessageLink";
        req["chat_id"] = chatId;
        req["message_id"] = messageId;
        json j = json::parse(client_.invoke(req.dump()), nullptr, false);
        if (!j.is_discarded() && j.is_object() &&
            strField(j, "@type") == "messageLink") {
            const std::string link = strField(j, "link");
            if (!link.empty()) return link;
        }
    }

    if (chatId <= -1000000000000LL) {
        const std::int64_t internalId = -1000000000000LL - chatId;
        return "https://t.me/c/" + std::to_string(internalId) + "/" +
               std::to_string(messageId >> 20);
    }
    return std::string();
}

std::string TelegramClient::getChatMemberStatus(std::int64_t chatId, std::int64_t userId) {
    {
        json g;
        g["@type"] = "getChat";
        g["chat_id"] = chatId;
        client_.invoke(g.dump(), 10000);
    }

    json req;
    req["@type"] = "getChatMember";
    req["chat_id"] = chatId;
    json member;
    member["@type"] = "messageSenderUser";
    member["user_id"] = userId;
    req["member_id"] = member;

    const std::string resp = client_.invoke(req.dump());
    json j = json::parse(resp, nullptr, false);
    if (!j.is_discarded() && j.is_object() && strField(j, "@type") == "chatMember" &&
        j.contains("status") && j["status"].is_object()) {
        return strField(j["status"], "@type");
    }
    return std::string();
}

}  // namespace anonx
