#pragma once
// WP-1 private helpers for the pcdrw/1 JSON conventions (DrawingIR.h header comment). Used by ir/ and eval/.
#include "pc/drawing/DrawingIR.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pc::drawing::jsonio {
using nlohmann::json;

// float -> shortest decimal that round-trips the float (0.9f is written 0.9, not 0.8999999761581421).
inline double fl(float f) {
  if (!std::isfinite(f)) return 0.0;
  char buf[32];
  for (int prec = 1; prec <= 9; ++prec) {
    std::snprintf(buf, sizeof buf, "%.*g", prec, static_cast<double>(f));
    if (std::strtof(buf, nullptr) == f) return std::strtod(buf, nullptr);
  }
  return static_cast<double>(f);
}
inline json num(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }
inline json vec(Vec2 v) { return json::array({num(v.x), num(v.y)}); }
inline json box(const Box2& b) { return json{{"lo", vec(b.lo)}, {"hi", vec(b.hi)}}; }
template <class E> inline json en(E e) { return json(toString(e)); }

inline double getD(const json& j, const char* k, double def = 0) {
  auto it = j.find(k);
  return (it != j.end() && it->is_number()) ? it->get<double>() : def;
}
inline float getF(const json& j, const char* k, float def = 0) { return static_cast<float>(getD(j, k, def)); }
inline int getI(const json& j, const char* k, int def = 0) {
  auto it = j.find(k);
  return (it != j.end() && it->is_number()) ? static_cast<int>(it->get<double>()) : def;
}
inline bool getB(const json& j, const char* k, bool def = false) {
  auto it = j.find(k);
  return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}
inline std::string getS(const json& j, const char* k, const std::string& def = {}) {
  auto it = j.find(k);
  return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}
inline Vec2 toVec(const json& j) {
  if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number()) return {j[0].get<double>(), j[1].get<double>()};
  return {};
}
inline Vec2 getV(const json& j, const char* k) { auto it = j.find(k); return it != j.end() ? toVec(*it) : Vec2{}; }
inline Box2 toBox(const json& j) {
  Box2 b{};
  if (j.is_object()) { if (j.contains("lo")) b.lo = toVec(j["lo"]); if (j.contains("hi")) b.hi = toVec(j["hi"]); }
  return b;
}
inline Box2 getBox(const json& j, const char* k) { auto it = j.find(k); return it != j.end() ? toBox(*it) : Box2{}; }
template <class E> inline E getE(const json& j, const char* k, E def) {
  auto it = j.find(k);
  if (it != j.end() && it->is_string()) { E e = def; if (fromString(it->get<std::string>(), e)) return e; }
  return def;
}
inline std::vector<int> getIds(const json& j, const char* k) {
  std::vector<int> v;
  auto it = j.find(k);
  if (it != j.end() && it->is_array()) for (auto& x : *it) if (x.is_number()) v.push_back(static_cast<int>(x.get<double>()));
  return v;
}
inline std::vector<double> getDv(const json& j, const char* k) {
  std::vector<double> v;
  auto it = j.find(k);
  if (it != j.end() && it->is_array()) for (auto& x : *it) if (x.is_number()) v.push_back(x.get<double>());
  return v;
}
inline json optD(const std::optional<double>& o) { return o ? num(*o) : json(nullptr); }
inline std::optional<double> getOpt(const json& j, const char* k) {
  auto it = j.find(k);
  if (it != j.end() && it->is_number()) return it->get<double>();
  return std::nullopt;
}
inline json attrs(const Attrs& a) { json j = json::object(); for (auto& [k, v] : a) j[k] = v; return j; }
inline Attrs getAttrs(const json& j, const char* k) {
  Attrs a;
  auto it = j.find(k);
  if (it != j.end() && it->is_object())
    for (auto& [kk, v] : it->items()) a[kk] = v.is_string() ? v.get<std::string>() : v.dump();
  return a;
}
inline json affine(const Affine2& m) { json j = json::array(); for (double v : m) j.push_back(num(v)); return j; }
inline Affine2 getAffine(const json& j, const char* k, const Affine2& def) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_array() || it->size() != 6) return def;
  Affine2 m{};
  for (int i = 0; i < 6; ++i) m[i] = (*it)[i].is_number() ? (*it)[i].get<double>() : 0;
  return m;
}

// Exposed so eval/ (gt.json) can reuse them.
json toJsonScale(const Scale& s);
Scale scaleFromJson(const json& j);
json toJsonDiag(const Diagnostic& d);
Diagnostic diagFromJson(const json& j);

}  // namespace pc::drawing::jsonio
