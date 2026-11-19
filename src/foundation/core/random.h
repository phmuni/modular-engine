#pragma once
// The engine's random number generator, shared by systems and games (main thread only).
//
// Seeded from the OS by default; Random::seed makes runs repeatable (--deterministic seeds it with 1).

#include <cstdint>
#include <random>

namespace Random {

inline std::mt19937 &generator() {
  static std::mt19937 generator{std::random_device{}()};
  return generator;
}

inline void seed(uint32_t value) { generator().seed(value); }

} // namespace Random
