/**
 * @file PdfObject.h
 * @brief A PDF object value: null/bool/number/name/string/array/dictionary/indirect-reference/stream.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class PdfObject {
 public:
  enum class Type : uint8_t { Null, Boolean, Integer, Real, Name, String, Array, Dictionary, Reference, Stream };

  Type type = Type::Null;
  bool boolValue = false;
  int64_t intValue = 0;
  double realValue = 0.0;
  std::string strValue;  // Name text, or decoded (raw) String bytes
  std::vector<PdfObject> arrValue;
  std::map<std::string, PdfObject> dictValue;  // used by Dictionary and Stream (the stream's own dict)
  uint32_t refNum = 0;
  uint16_t refGen = 0;

  // Stream-only: location of the raw (still filter-encoded) bytes within the document's in-memory buffer.
  size_t streamOffset = 0;
  size_t streamLength = 0;

  PdfObject() = default;

  static PdfObject makeNull() { return PdfObject(); }
  static PdfObject makeBool(bool v) {
    PdfObject o;
    o.type = Type::Boolean;
    o.boolValue = v;
    return o;
  }
  static PdfObject makeInt(int64_t v) {
    PdfObject o;
    o.type = Type::Integer;
    o.intValue = v;
    return o;
  }
  static PdfObject makeReal(double v) {
    PdfObject o;
    o.type = Type::Real;
    o.realValue = v;
    return o;
  }
  static PdfObject makeName(std::string v) {
    PdfObject o;
    o.type = Type::Name;
    o.strValue = std::move(v);
    return o;
  }
  static PdfObject makeString(std::string v) {
    PdfObject o;
    o.type = Type::String;
    o.strValue = std::move(v);
    return o;
  }
  static PdfObject makeRef(uint32_t num, uint16_t gen) {
    PdfObject o;
    o.type = Type::Reference;
    o.refNum = num;
    o.refGen = gen;
    return o;
  }

  bool isNull() const { return type == Type::Null; }
  bool isNumber() const { return type == Type::Integer || type == Type::Real; }
  bool isName() const { return type == Type::Name; }
  bool isString() const { return type == Type::String; }
  bool isArray() const { return type == Type::Array; }
  bool isDict() const { return type == Type::Dictionary || type == Type::Stream; }
  bool isStream() const { return type == Type::Stream; }
  bool isReference() const { return type == Type::Reference; }

  double asNumber(double defaultValue = 0.0) const {
    if (type == Type::Integer) return static_cast<double>(intValue);
    if (type == Type::Real) return realValue;
    return defaultValue;
  }
  int asInt(int defaultValue = 0) const {
    if (type == Type::Integer) return static_cast<int>(intValue);
    if (type == Type::Real) return static_cast<int>(realValue);
    return defaultValue;
  }

  // Direct (unresolved) dictionary lookup - does not follow indirect references. Returns nullptr if absent
  // or this object isn't a dictionary/stream.
  const PdfObject* find(const std::string& key) const {
    if (!isDict()) return nullptr;
    const auto it = dictValue.find(key);
    return it == dictValue.end() ? nullptr : &it->second;
  }
};
