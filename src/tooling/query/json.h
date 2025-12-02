/**
 * @file json.h
 * @brief Minimal JSON serialization helpers (header-only)
 *
 * This provides a simple, dependency-free JSON builder for the query tool.
 * It only supports serialization (writing), not parsing.
 *
 * Example:
 *   JsonObject obj;
 *   obj.set("name", "test");
 *   obj.set("count", 42);
 *   obj.set("active", true);
 *   std::string json = obj.toString();
 *   // {"name":"test","count":42,"active":true}
 */

#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace ascii_query {
namespace json {

/**
 * @brief Escape a string for JSON output
 */
inline std::string escape(const std::string &str) {
  std::string result;
  result.reserve(str.size() + 16);

  for (char c : str) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        // Control character - escape as \uXXXX
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        result += buf;
      } else {
        result += c;
      }
      break;
    }
  }

  return result;
}

// Forward declarations
class JsonArray;
class JsonObject;

/**
 * @brief JSON value wrapper (can hold any JSON type)
 */
class JsonValue {
public:
  enum class Type { Null, Bool, Int, UInt, Double, String, Array, Object };

  JsonValue() : type_(Type::Null) {}
  JsonValue(std::nullptr_t) : type_(Type::Null) {}
  JsonValue(bool b) : type_(Type::Bool), bool_val_(b) {}
  JsonValue(int i) : type_(Type::Int), int_val_(i) {}
  JsonValue(int64_t i) : type_(Type::Int), int_val_(i) {}
  JsonValue(uint64_t u) : type_(Type::UInt), uint_val_(u) {}
  JsonValue(double d) : type_(Type::Double), double_val_(d) {}
  JsonValue(const char *s) : type_(Type::String), str_val_(s) {}
  JsonValue(const std::string &s) : type_(Type::String), str_val_(s) {}
  JsonValue(std::string &&s) : type_(Type::String), str_val_(std::move(s)) {}

  // Array and Object are stored as pre-serialized strings
  static JsonValue fromArray(const std::string &serialized) {
    JsonValue v;
    v.type_ = Type::Array;
    v.str_val_ = serialized;
    return v;
  }

  static JsonValue fromObject(const std::string &serialized) {
    JsonValue v;
    v.type_ = Type::Object;
    v.str_val_ = serialized;
    return v;
  }

  [[nodiscard]] std::string toString() const {
    switch (type_) {
    case Type::Null:
      return "null";
    case Type::Bool:
      return bool_val_ ? "true" : "false";
    case Type::Int:
      return std::to_string(int_val_);
    case Type::UInt:
      return std::to_string(uint_val_);
    case Type::Double: {
      std::ostringstream oss;
      oss << double_val_;
      return oss.str();
    }
    case Type::String:
      return "\"" + escape(str_val_) + "\"";
    case Type::Array:
    case Type::Object:
      return str_val_; // Already serialized
    }
    return "null";
  }

private:
  Type type_;
  bool bool_val_ = false;
  int64_t int_val_ = 0;
  uint64_t uint_val_ = 0;
  double double_val_ = 0.0;
  std::string str_val_;
};

/**
 * @brief JSON array builder
 */
class JsonArray {
public:
  JsonArray() = default;

  JsonArray &add(const JsonValue &value) {
    values_.push_back(value);
    return *this;
  }

  JsonArray &add(const JsonArray &arr) {
    values_.push_back(JsonValue::fromArray(arr.toString()));
    return *this;
  }

  JsonArray &add(const JsonObject &obj);

  [[nodiscard]] std::string toString() const {
    std::string result = "[";
    bool first = true;
    for (const auto &v : values_) {
      if (!first)
        result += ",";
      first = false;
      result += v.toString();
    }
    result += "]";
    return result;
  }

  [[nodiscard]] bool empty() const {
    return values_.empty();
  }
  [[nodiscard]] size_t size() const {
    return values_.size();
  }

private:
  std::vector<JsonValue> values_;
};

/**
 * @brief JSON object builder
 */
class JsonObject {
public:
  JsonObject() = default;

  JsonObject &set(const std::string &key, const JsonValue &value) {
    keys_.push_back(key);
    values_.push_back(value);
    return *this;
  }

  JsonObject &set(const std::string &key, const JsonArray &arr) {
    keys_.push_back(key);
    values_.push_back(JsonValue::fromArray(arr.toString()));
    return *this;
  }

  JsonObject &set(const std::string &key, const JsonObject &obj) {
    keys_.push_back(key);
    values_.push_back(JsonValue::fromObject(obj.toString()));
    return *this;
  }

  [[nodiscard]] std::string toString() const {
    std::string result = "{";
    for (size_t i = 0; i < keys_.size(); i++) {
      if (i > 0)
        result += ",";
      result += "\"" + escape(keys_[i]) + "\":" + values_[i].toString();
    }
    result += "}";
    return result;
  }

  [[nodiscard]] bool empty() const {
    return keys_.empty();
  }
  [[nodiscard]] size_t size() const {
    return keys_.size();
  }

private:
  std::vector<std::string> keys_;
  std::vector<JsonValue> values_;
};

// Deferred implementation
inline JsonArray &JsonArray::add(const JsonObject &obj) {
  values_.push_back(JsonValue::fromObject(obj.toString()));
  return *this;
}

} // namespace json
} // namespace ascii_query
