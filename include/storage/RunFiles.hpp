/*
 * RunFiles.hpp — pure helpers for per-run SD file naming and labels.
 * No Arduino dependencies, so the firmware and host (Unity) tests share one
 * implementation. Paths use the /runs/NNNNN.csv layout.
 */
#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace RunFiles {

inline std::string pad5(int id) {
  std::string s = std::to_string(id < 0 ? 0 : id);
  if (s.size() < 5) s.insert(0, 5 - s.size(), '0');
  return s;
}

inline std::string csvPath(int id)  { return "/runs/" + pad5(id) + ".csv"; }
inline std::string namePath(int id) { return "/runs/" + pad5(id) + ".name"; }

// Return the run id if `nameOrPath`'s basename is exactly NNNNN.csv (N = digit),
// else -1. Accepts both "00007.csv" and "/runs/00007.csv".
inline int parseId(const std::string& nameOrPath) {
  const std::size_t slash = nameOrPath.find_last_of('/');
  const std::string base =
      (slash == std::string::npos) ? nameOrPath : nameOrPath.substr(slash + 1);
  const std::string suffix = ".csv";
  if (base.size() <= suffix.size()) return -1;
  if (base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0) return -1;
  const std::string stem = base.substr(0, base.size() - suffix.size());
  if (stem.empty()) return -1;
  for (char c : stem) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
  return std::stoi(stem);
}

inline int nextId(const std::vector<int>& ids) {
  int mx = 0;
  for (int id : ids) if (id > mx) mx = id;
  return mx + 1;
}

inline std::string sanitizeName(const std::string& raw, std::size_t maxLen = 32) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);  // drop control chars
  }
  const std::size_t b = out.find_first_not_of(' ');
  const std::size_t e = out.find_last_not_of(' ');
  out = (b == std::string::npos) ? "" : out.substr(b, e - b + 1);
  if (out.size() > maxLen) out.resize(maxLen);
  return out;
}

inline std::string label(int id, const std::string& name) {
  return name.empty() ? ("Run " + std::to_string(id)) : name;
}

}  // namespace RunFiles
