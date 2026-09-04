#ifndef ANONX_LANG_HPP
#define ANONX_LANG_HPP

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace anonx {

std::string formatStr(const std::string& tmpl, const std::vector<std::string>& args);

inline std::string toArg(const std::string& s) { return s; }
inline std::string toArg(const char* s) { return s ? std::string(s) : std::string(); }
inline std::string toArg(bool b) { return b ? "true" : "false"; }
template <typename T,
          typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline std::string toArg(T v) { return std::to_string(v); }

class Language;

class LangView {
public:
    LangView() = default;
    LangView(const Language* lang, std::string code)
        : lang_(lang), code_(std::move(code)) {}

    std::string operator[](const std::string& key) const;
    std::string get(const std::string& key) const { return (*this)[key]; }

    template <typename... Args>
    std::string fmt(const std::string& key, Args&&... args) const {
        return formatStr((*this)[key],
                         std::vector<std::string>{ toArg(std::forward<Args>(args))... });
    }

    const std::string& code() const { return code_; }
    bool valid() const { return lang_ != nullptr; }

private:
    const Language* lang_ = nullptr;
    std::string code_;
};

class Language {
public:
    Language();
    ~Language();

    Language(const Language&)            = delete;
    Language& operator=(const Language&) = delete;

    bool loadFile(const std::string& code, const std::string& path);

    bool loadJsonText(const std::string& code, const std::string& jsonText);

    int loadDir(const std::string& dir);

    bool loaded(const std::string& code) const;

    std::vector<std::string> codes() const;

    void setDefault(const std::string& code) { defaultCode_ = code; }
    const std::string& defaultCode() const { return defaultCode_; }

    std::string tr(const std::string& code, const std::string& key) const;

    LangView view(const std::string& code) const;

    static std::string nameOf(const std::string& code);

    static const std::vector<std::pair<std::string, std::string>>& allCodes();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string defaultCode_ = "en";
};

}

#endif
