#pragma once
// Minimal, self-contained JSON library for NPC state persistence.
// No external dependencies — C++17 only.

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace npc::serial {

// ═══════════════════════════════════════════════════════════════════════
// JsonValue
// ═══════════════════════════════════════════════════════════════════════

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray  = std::vector<JsonValue>;

class JsonValue {
public:
    using Var = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        JsonArray,
        JsonObject
    >;

    JsonValue()                    : v_(nullptr) {}
    JsonValue(std::nullptr_t)      : v_(nullptr) {}
    JsonValue(bool b)              : v_(b) {}
    JsonValue(int i)               : v_(static_cast<int64_t>(i)) {}
    JsonValue(unsigned u)          : v_(static_cast<int64_t>(u)) {}
    JsonValue(int64_t i)           : v_(i) {}
    JsonValue(uint64_t u)          : v_(static_cast<int64_t>(u)) {}
    JsonValue(double d)            : v_(d) {}
    JsonValue(float f)             : v_(static_cast<double>(f)) {}
    JsonValue(const char* s)       : v_(std::string(s)) {}
    JsonValue(std::string s)       : v_(std::move(s)) {}
    JsonValue(JsonArray  a)        : v_(std::move(a)) {}
    JsonValue(JsonObject o)        : v_(std::move(o)) {}

    // ── Type queries ────────────────────────────────────────────────
    bool isNull()   const { return std::holds_alternative<std::nullptr_t>(v_); }
    bool isBool()   const { return std::holds_alternative<bool>(v_); }
    bool isInt()    const { return std::holds_alternative<int64_t>(v_); }
    bool isDouble() const { return std::holds_alternative<double>(v_); }
    bool isString() const { return std::holds_alternative<std::string>(v_); }
    bool isArray()  const { return std::holds_alternative<JsonArray>(v_); }
    bool isObject() const { return std::holds_alternative<JsonObject>(v_); }
    bool isNumber() const { return isInt() || isDouble(); }

    // ── Value access ────────────────────────────────────────────────
    bool        asBool  (bool        def = false) const {
        return isBool() ? std::get<bool>(v_) : def;
    }
    int64_t     asInt   (int64_t     def = 0)     const {
        if (isInt())    return std::get<int64_t>(v_);
        if (isDouble()) return static_cast<int64_t>(std::get<double>(v_));
        return def;
    }
    double      asDouble(double      def = 0.0)   const {
        if (isDouble()) return std::get<double>(v_);
        if (isInt())    return static_cast<double>(std::get<int64_t>(v_));
        return def;
    }
    float       asFloat (float       def = 0.f)   const {
        return static_cast<float>(asDouble(static_cast<double>(def)));
    }
    const std::string& asString() const {
        static const std::string empty;
        return isString() ? std::get<std::string>(v_) : empty;
    }
    const JsonArray&  asArray()  const {
        static const JsonArray  empty;
        return isArray()  ? std::get<JsonArray>(v_)  : empty;
    }
    const JsonObject& asObject() const {
        static const JsonObject empty;
        return isObject() ? std::get<JsonObject>(v_) : empty;
    }

    // ── Object helpers ──────────────────────────────────────────────
    bool has(const std::string& k) const {
        return isObject() && std::get<JsonObject>(v_).count(k);
    }
    const JsonValue& operator[](const std::string& k) const {
        static const JsonValue null_;
        if (!isObject()) return null_;
        auto it = std::get<JsonObject>(v_).find(k);
        return it != std::get<JsonObject>(v_).end() ? it->second : null_;
    }
    JsonValue& operator[](const std::string& k) {
        return std::get<JsonObject>(v_)[k];
    }
    // Array access
    const JsonValue& operator[](size_t i) const {
        static const JsonValue null_;
        if (!isArray()) return null_;
        auto& a = std::get<JsonArray>(v_);
        return i < a.size() ? a[i] : null_;
    }
    size_t size() const {
        if (isArray())  return std::get<JsonArray>(v_).size();
        if (isObject()) return std::get<JsonObject>(v_).size();
        return 0;
    }
    bool empty() const { return size() == 0; }

    const Var& var() const { return v_; }
    Var&       var()       { return v_; }

private:
    Var v_;
};

// ─── Convenience constructors ─────────────────────────────────────────
inline JsonObject obj() { return {}; }
inline JsonArray  arr() { return {}; }

// ═══════════════════════════════════════════════════════════════════════
// Writer
// ═══════════════════════════════════════════════════════════════════════

namespace detail {

inline std::string escStr(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b,8,"\\u%04x",c); o+=b; }
                else o += static_cast<char>(c);
        }
    }
    o += '"';
    return o;
}

inline void write(const JsonValue& v, std::string& out, int ind, int d) {
    const std::string nl  = ind > 0 ? "\n"  : "";
    const std::string sp  = ind > 0 ? std::string( d      * ind, ' ') : "";
    const std::string sp1 = ind > 0 ? std::string((d + 1) * ind, ' ') : "";

    switch (v.var().index()) {
        case 0: out += "null";  return;
        case 1: out += v.asBool() ? "true" : "false"; return;
        case 2: out += std::to_string(v.asInt()); return;
        case 3: {
            char buf[64];
            double x = v.asDouble();
            if (std::isfinite(x) && x == static_cast<double>(static_cast<int64_t>(x))
                && std::abs(x) < 1e15)
                std::snprintf(buf, sizeof(buf), "%.1f", x);
            else
                std::snprintf(buf, sizeof(buf), "%.8g", x);
            out += buf; return;
        }
        case 4: out += escStr(v.asString()); return;
        case 5: { // array
            auto& a = v.asArray();
            if (a.empty()) { out += "[]"; return; }
            out += "[" + nl;
            for (size_t i = 0; i < a.size(); ++i) {
                out += sp1; write(a[i], out, ind, d+1);
                if (i+1 < a.size()) out += ",";
                out += nl;
            }
            out += sp + "]"; return;
        }
        case 6: { // object
            auto& o = v.asObject();
            if (o.empty()) { out += "{}"; return; }
            out += "{" + nl;
            size_t i = 0;
            for (auto& [k, val] : o) {
                out += sp1 + escStr(k) + (ind > 0 ? ": " : ":");
                write(val, out, ind, d+1);
                if (i+1 < o.size()) out += ",";
                out += nl; ++i;
            }
            out += sp + "}"; return;
        }
    }
}

} // namespace detail

inline std::string toString(const JsonValue& v, bool pretty = true) {
    std::string s;
    detail::write(v, s, pretty ? 2 : 0, 0);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════
// Parser
// ═══════════════════════════════════════════════════════════════════════

namespace detail {

struct Parser {
    const char* p;
    const char* end;

    explicit Parser(std::string_view s) : p(s.data()), end(s.data()+s.size()) {}

    void skip() {
        while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p;
    }
    char peek() { skip(); return p < end ? *p : '\0'; }
    char consume() { skip(); return p < end ? *p++ : '\0'; }

    std::string parseString() {
        ++p; // opening "
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                ++p;
                switch (*p) {
                    case '"': s+='"'; break; case '\\': s+='\\'; break;
                    case '/': s+='/'; break; case 'n': s+='\n'; break;
                    case 'r': s+='\r'; break; case 't': s+='\t'; break;
                    case 'b': s+='\b'; break; case 'f': s+='\f'; break;
                    case 'u': {
                        char h[5]={};
                        for (int i=0;i<4&&p+1<end;++i) h[i]=*++p;
                        unsigned cp = static_cast<unsigned>(std::strtoul(h,nullptr,16));
                        if (cp < 0x80) s += static_cast<char>(cp);
                        else if (cp < 0x800) { s+=static_cast<char>(0xC0|(cp>>6)); s+=static_cast<char>(0x80|(cp&0x3F)); }
                        else { s+=static_cast<char>(0xE0|(cp>>12)); s+=static_cast<char>(0x80|((cp>>6)&0x3F)); s+=static_cast<char>(0x80|(cp&0x3F)); }
                        break;
                    }
                    default: s += *p;
                }
                ++p;
            } else { s += *p++; }
        }
        if (p < end) ++p; // closing "
        return s;
    }

    JsonValue parseNumber() {
        const char* start = p;
        bool flt = false;
        if (*p=='-') ++p;
        while (p<end && *p>='0' && *p<='9') ++p;
        if (p<end && *p=='.') { flt=true; ++p; while(p<end&&*p>='0'&&*p<='9') ++p; }
        if (p<end && (*p=='e'||*p=='E')) { flt=true; ++p; if(*p=='+'||*p=='-') ++p; while(p<end&&*p>='0'&&*p<='9') ++p; }
        std::string num(start, p);
        if (flt) return std::stod(num);
        return static_cast<int64_t>(std::stoll(num));
    }

    JsonValue parseValue() {
        char c = peek();
        if (c=='"') return parseString();
        if (c=='{') return parseObject();
        if (c=='[') return parseArray();
        if (c=='t') { p+=4; return true; }
        if (c=='f') { p+=5; return false; }
        if (c=='n') { p+=4; return nullptr; }
        if (c=='-'||(c>='0'&&c<='9')) return parseNumber();
        throw std::runtime_error(std::string("JSON: unexpected '") + c + "'");
    }

    JsonValue parseObject() {
        ++p; // {
        JsonObject o;
        if (peek()=='}') { ++p; return o; }
        while (true) {
            skip();
            if (peek()!='"') throw std::runtime_error("JSON: expected key string");
            std::string key = parseString();
            skip();
            if (consume()!=':') throw std::runtime_error("JSON: expected ':'");
            o[key] = parseValue();
            skip();
            char ch = peek();
            if (ch=='}') { ++p; break; }
            if (ch==',') { ++p; continue; }
            throw std::runtime_error("JSON: expected ',' or '}'");
        }
        return o;
    }

    JsonValue parseArray() {
        ++p; // [
        JsonArray a;
        if (peek()==']') { ++p; return a; }
        while (true) {
            a.push_back(parseValue());
            skip();
            char ch = peek();
            if (ch==']') { ++p; break; }
            if (ch==',') { ++p; continue; }
            throw std::runtime_error("JSON: expected ',' or ']'");
        }
        return a;
    }
};

} // namespace detail

inline JsonValue parse(std::string_view s) {
    detail::Parser p(s);
    return p.parseValue();
}

// ─── File I/O ─────────────────────────────────────────────────────────

inline bool saveFile(const JsonValue& v, const std::string& path, bool pretty = true) {
    std::ofstream f(path);
    if (!f) return false;
    f << toString(v, pretty);
    return f.good();
}

inline JsonValue loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string s((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
    return parse(s);
}

inline bool tryLoadFile(const std::string& path, JsonValue& out) {
    try { out = loadFile(path); return true; }
    catch (...) { return false; }
}

} // namespace npc::serial
