#include "neuro_json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vj {
namespace neuro {

Json Json::boolean(bool b) {
    Json j;
    j.type_ = Bool;
    j.b_ = b;
    return j;
}

Json Json::number(double d) {
    Json j;
    j.type_ = Number;
    j.n_ = d;
    return j;
}

Json Json::string(const std::string& s) {
    Json j;
    j.type_ = String;
    j.s_ = s;
    return j;
}

Json Json::array() {
    Json j;
    j.type_ = Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Object;
    return j;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json kNull;
    if (type_ != Object) return kNull;
    auto it = obj_.find(key);
    return it == obj_.end() ? kNull : it->second;
}

Json& Json::set(const std::string& key, Json v) {
    if (type_ == Object) obj_[key] = std::move(v);
    return *this;
}

Json& Json::push(Json v) {
    if (type_ == Array) arr_.push_back(std::move(v));
    return *this;
}

// ---------------------------------------------------------------------------------------------------------------
// writer
// ---------------------------------------------------------------------------------------------------------------
std::string jsonQuote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o.push_back((char)c);
            }
        }
    }
    return o + "\"";
}

static std::string numberText(double d) {
    if (!std::isfinite(d)) return "null";
    char buf[32];
    if (d == std::floor(d) && std::fabs(d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", d);
    else std::snprintf(buf, sizeof buf, "%.17g", d);
    return buf;
}

std::string Json::dump() const {
    switch (type_) {
    case Null: return "null";
    case Bool: return b_ ? "true" : "false";
    case Number: return numberText(n_);
    case String: return jsonQuote(s_);
    case Array: {
        std::string o = "[";
        for (size_t i = 0; i < arr_.size(); i++) o += (i ? "," : "") + arr_[i].dump();
        return o + "]";
    }
    case Object: {
        std::string o = "{";
        bool first = true;
        for (const auto& kv : obj_) {
            o += (first ? "" : ",") + jsonQuote(kv.first) + ":" + kv.second.dump();
            first = false;
        }
        return o + "}";
    }
    }
    return "null";
}

// ---------------------------------------------------------------------------------------------------------------
// reader
// ---------------------------------------------------------------------------------------------------------------
class JsonParser {
public:
    explicit JsonParser(const std::string& t) : t_(t) {}

    bool document(Json& out, std::string& err) {
        skipWs();
        if (!value(out, 0)) {
            err = err_ + " at byte " + std::to_string(p_);
            return false;
        }
        skipWs();
        if (p_ != t_.size()) {
            err = "trailing characters at byte " + std::to_string(p_);
            return false;
        }
        return true;
    }

private:
    const std::string& t_;
    size_t p_ = 0;
    std::string err_;

    bool fail(const char* m) {
        err_ = m;
        return false;
    }
    void skipWs() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) p_++;
    }
    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) n++;
        if (t_.compare(p_, n, word) != 0) return fail("bad literal");
        p_ += n;
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > Json::kMaxDepth) return fail("nested too deeply");
        if (p_ >= t_.size()) return fail("unexpected end");
        const char c = t_[p_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type_ = Json::String;
            return string(out.s_);
        }
        if (c == 't') return (out = Json::boolean(true), literal("true"));
        if (c == 'f') return (out = Json::boolean(false), literal("false"));
        if (c == 'n') return (out = Json(), literal("null"));
        return number(out);
    }

    bool number(Json& out) {
        const size_t start = p_;
        if (p_ < t_.size() && t_[p_] == '-') p_++;
        while (p_ < t_.size() && (std::isdigit((unsigned char)t_[p_]) || t_[p_] == '.' || t_[p_] == 'e' ||
                                  t_[p_] == 'E' || t_[p_] == '+' || t_[p_] == '-'))
            p_++;
        if (p_ == start) return fail("unexpected character");
        const std::string s = t_.substr(start, p_ - start);
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (!end || *end != '\0') return fail("bad number");
        out = Json::number(d);
        return true;
    }

    static void putUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) {
            o.push_back((char)cp);
        } else if (cp < 0x800) {
            o.push_back((char)(0xC0 | (cp >> 6)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            o.push_back((char)(0xE0 | (cp >> 12)));
            o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            o.push_back((char)(0xF0 | (cp >> 18)));
            o.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(unsigned& v) {
        if (p_ + 4 > t_.size()) return fail("short \\u escape");
        v = 0;
        for (int i = 0; i < 4; i++) {
            const char h = t_[p_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool unicodeEscape(std::string& o) {
        unsigned cp = 0;
        if (!hex4(cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF && t_.compare(p_, 2, "\\u") == 0) {
            p_ += 2;
            unsigned lo = 0;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            else putUtf8(o, 0xFFFD), cp = lo;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // an unpaired surrogate
        putUtf8(o, cp);
        return true;
    }

    bool escape(std::string& o) {
        if (p_ >= t_.size()) return fail("unterminated string");
        const char e = t_[p_++];
        switch (e) {
        case '"': o.push_back('"'); return true;
        case '\\': o.push_back('\\'); return true;
        case '/': o.push_back('/'); return true;
        case 'b': o.push_back('\b'); return true;
        case 'f': o.push_back('\f'); return true;
        case 'n': o.push_back('\n'); return true;
        case 'r': o.push_back('\r'); return true;
        case 't': o.push_back('\t'); return true;
        case 'u': return unicodeEscape(o);
        default: return fail("bad escape");
        }
    }

    bool string(std::string& o) {
        p_++;   // the opening quote
        o.clear();
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (!escape(o)) return false;
            } else if ((unsigned char)c < 0x20) {
                return fail("control character in string");
            } else {
                o.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool array(Json& out, int depth) {
        out = Json::array();
        p_++;
        skipWs();
        if (p_ < t_.size() && t_[p_] == ']') return (p_++, true);
        for (;;) {
            Json v;
            skipWs();
            if (!value(v, depth + 1)) return false;
            out.arr_.push_back(std::move(v));
            skipWs();
            if (p_ >= t_.size()) return fail("unterminated array");
            const char c = t_[p_++];
            if (c == ']') return true;
            if (c != ',') return fail("expected , or ]");
        }
    }

    bool object(Json& out, int depth) {
        out = Json::object();
        p_++;
        skipWs();
        if (p_ < t_.size() && t_[p_] == '}') return (p_++, true);
        for (;;) {
            skipWs();
            std::string key;
            if (p_ >= t_.size() || t_[p_] != '"') return fail("expected a key");
            if (!string(key)) return false;
            skipWs();
            if (p_ >= t_.size() || t_[p_++] != ':') return fail("expected :");
            skipWs();
            Json v;
            if (!value(v, depth + 1)) return false;
            out.obj_[key] = std::move(v);
            skipWs();
            if (p_ >= t_.size()) return fail("unterminated object");
            const char c = t_[p_++];
            if (c == '}') return true;
            if (c != ',') return fail("expected , or }");
        }
    }
};

bool Json::parse(const std::string& text, Json& out, std::string& err) {
    JsonParser p(text);
    Json v;
    if (!p.document(v, err)) return false;
    out = std::move(v);
    return true;
}

}  // namespace neuro
}  // namespace vj
