// A small JSON reader and writer for the Neuro API client (neuro_*.cpp).
//
// The Neuro API (https://github.com/VedalAI/neuro-sdk, API/SPECIFICATION.md) sends JSON text frames, and an
// action's own parameters arrive as a JSON *string* inside one ("data": "{\"buttons\":[\"b\"]}") that comes
// straight from the model, so it has to be parsed defensively: parse() never throws and never recurses past
// kMaxDepth, and a malformed input is reported, not guessed at.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vj {
namespace neuro {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    static constexpr int kMaxDepth = 32;

    Json() = default;
    static Json boolean(bool b);
    static Json number(double d);
    static Json string(const std::string& s);
    static Json array();
    static Json object();

    // Parses a whole document (surrounding whitespace allowed, nothing else after it).  On failure returns
    // false and sets `err` to a short message with the byte offset.
    static bool parse(const std::string& text, Json& out, std::string& err);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isBool() const { return type_ == Bool; }
    bool isNumber() const { return type_ == Number; }
    bool isString() const { return type_ == String; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }

    bool asBool() const { return b_; }
    double asNumber() const { return n_; }
    const std::string& asString() const { return s_; }
    const std::vector<Json>& items() const { return arr_; }
    const std::map<std::string, Json>& members() const { return obj_; }

    // Object member, or a shared Null when absent / not an object.
    const Json& operator[](const std::string& key) const;
    bool has(const std::string& key) const { return type_ == Object && obj_.count(key) != 0; }

    // Builders (no-ops on the wrong type).
    Json& set(const std::string& key, Json v);
    Json& push(Json v);

    std::string dump() const;   // compact; keys in sorted order

private:
    Type type_ = Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;

    friend class JsonParser;
};

std::string jsonQuote(const std::string& s);   // "..." with the JSON escapes

}  // namespace neuro
}  // namespace vj
