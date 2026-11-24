#pragma once
// Random number generator of a run.
//
// Every random decision of GSO-RL, and of the macro-moves of an adapter, should come from this
// generator. Two reasons:
//   1. One seed reproduces a whole run.
//   2. The distributions of <random> (uniform_int_distribution, ...) are implementation-defined:
//      the same seed gives different numbers with libstdc++, libc++ and MSVC. The helpers below
//      give the same numbers everywhere.
//
// Engine: xoshiro256** (Blackman & Vigna), seeded through splitmix64.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gso {

class Rng {
 public:
  using result_type = std::uint64_t;

  explicit Rng(std::uint64_t seed = 1) { reseed(seed); }

  void reseed(std::uint64_t seed) {
    std::uint64_t x = seed;
    for (auto& word : state_) word = splitmix64(x);
  }

  // Raw 64-bit value.
  std::uint64_t next() {
    const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
    const std::uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
  }

  // Lets standard algorithms use the generator (UniformRandomBitGenerator).
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return ~result_type(0); }
  result_type operator()() { return next(); }

  // Real number in [0, 1).
  double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

  // Real number in [a, b).
  double uniform(double a, double b) { return a + (b - a) * uniform(); }

  // Integer in [0, n), n > 0. Unbiased (rejection sampling).
  std::size_t below(std::size_t n) {
    const std::uint64_t bound = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = (0 - bound) % bound;  // 2^64 mod n
    std::uint64_t x = next();
    while (x < threshold) x = next();
    return static_cast<std::size_t>(x % bound);
  }

  // Integer in [a, b], a <= b.
  int uniform_int(int a, int b) { return a + static_cast<int>(below(static_cast<std::size_t>(b - a) + 1)); }

  // true with probability p.
  bool bernoulli(double p) { return uniform() < p; }

  // Standard normal N(0, 1) (Box-Muller).
  double normal() {
    const double u1 = 1.0 - uniform();  // (0, 1]
    const double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
  }

  // Gamma(shape, 1), shape > 0 (Marsaglia and Tsang, 2000).
  double gamma(double shape) {
    if (shape < 1.0) return gamma(shape + 1.0) * std::pow(1.0 - uniform(), 1.0 / shape);  // (0, 1]^(1/shape)
    const double d = shape - 1.0 / 3.0, c = 1.0 / std::sqrt(9.0 * d);
    for (;;) {
      double x, v;
      do {
        x = normal();
        v = 1.0 + c * x;
      } while (v <= 0);
      v = v * v * v;
      const double u = 1.0 - uniform();
      if (std::log(u) < 0.5 * x * x + d - d * v + d * std::log(v)) return d * v;
    }
  }

  // Beta(a, b), a, b > 0.
  double beta(double a, double b) {
    const double x = gamma(a);
    return x / (x + gamma(b));
  }

  // Index chosen with probability proportional to weights[i] (non-negative, not all zero).
  std::size_t weighted(const std::vector<double>& weights) {
    double total = 0;
    for (double w : weights) total += w;
    double x = uniform() * total;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      x -= weights[i];
      if (x < 0) return i;
    }
    return weights.size() - 1;
  }

  // Random element of a non-empty vector.
  template <class T>
  const T& pick(const std::vector<T>& v) { return v[below(v.size())]; }

  // Fisher-Yates shuffle.
  template <class T>
  void shuffle(std::vector<T>& v) {
    for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[below(i)]);
  }

 private:
  static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

  static std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  std::uint64_t state_[4];
};

}  // namespace gso
