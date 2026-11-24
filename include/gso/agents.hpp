#pragma once
// Learning agents (docs/04-learning-agents.md).
//
// One agent per ring of each guide chooses which macro-move to apply. An agent sees only its own
// history: which macro-moves it chose and the rewards that came back, possibly many steps later
// (Current Context Memory).
//
// Built-in agents, selected with the parameter agent.type:
//   ucb1(c=1.0)                          UCB1, the agent of the article
//   epsilon_greedy(eps=0.1)              best mean with probability 1 - eps, random otherwise
//   softmax(tau=0.1)                     Boltzmann exploration
//   thompson                             Thompson sampling, Gaussian model (adapts to the reward scale)
//   exp3(gamma=0.1)                      EXP3, robust to adversarial rewards (scale-free)
//   sliding_window_ucb(c=0.5, window=300)  UCB1 on recent evidence only (rewards that change)
//   discounted_ucb(c=0.5, gamma=0.99)    UCB1 with discounted statistics (rewards that change)
//   gradient(alpha=0.1)                  gradient bandit (preferences + softmax, scale-free)
//   random                               uniform choice (ablation GSO-Random of the article)
//   fixed(move=<name|index>)             always the same macro-move (baseline per macro-move)
// Your own: gso::register_agent("name", factory), then agent.type = "name(...)".

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "params.hpp"
#include "rng.hpp"

namespace gso {

class Agent {
 public:
  virtual ~Agent() = default;
  // Macro-move to apply now (index in the adapter's list).
  virtual int select(Rng& rng) = 0;
  // Delayed reward of a macro-move this agent chose earlier.
  virtual void update(int move, double reward) = 0;
  // The guide of this agent was replaced: forget everything...
  virtual void reset() = 0;
  // ...or keep a fraction lambda of the experience (agent.on_replace = decay(lambda)).
  virtual void decay(double lambda) = 0;
};

// Bookkeeping shared by the built-in agents: pulls and received rewards per macro-move.
//
// Pulls are counted when a macro-move is chosen; rewards arrive later. Keeping both apart is what
// makes a bandit work with delayed rewards: the exploration term grows with the pulls, so the agent
// does not keep choosing the same macro-move while its rewards are still on the way.
class BanditAgent : public Agent {
 public:
  explicit BanditAgent(int moves) : pulls_(moves, 0.0), rewarded_(moves, 0.0), sum_(moves, 0.0), sum2_(moves, 0.0) {}

  void update(int move, double reward) override {
    rewarded_[move] += 1;
    sum_[move] += reward;
    sum2_[move] += reward * reward;
  }
  void reset() override { scale(0.0); }
  void decay(double lambda) override { scale(lambda); }

  int moves() const { return static_cast<int>(pulls_.size()); }
  double pulls(int m) const { return pulls_[m]; }
  double rewarded(int m) const { return rewarded_[m]; }
  double mean(int m) const { return rewarded_[m] > 0 ? sum_[m] / rewarded_[m] : 0.0; }
  // Sample variance of the rewards of m, or -1 with fewer than two rewards.
  double variance(int m) const {
    const double n = rewarded_[m];
    if (n < 2) return -1;
    return std::max(0.0, (sum2_[m] - sum_[m] * sum_[m] / n) / (n - 1));
  }

 protected:
  int record(int move) {
    pulls_[move] += 1;
    total_pulls_ += 1;
    return move;
  }
  // A macro-move never chosen, picked at random (or -1 if all have been tried). Random, not the
  // first one: agents are reset often (every time their guide is replaced), and trying the
  // macro-moves always in the same order would favour the first one.
  int untried(Rng& rng) const {
    int count = 0;
    for (double p : pulls_) count += p == 0;
    if (count == 0) return -1;
    int k = static_cast<int>(rng.below(static_cast<std::size_t>(count)));
    for (int m = 0; m < moves(); ++m)
      if (pulls_[m] == 0 && k-- == 0) return m;
    return -1;
  }
  int best_mean() const {
    int best = 0;
    for (int m = 1; m < moves(); ++m)
      if (mean(m) > mean(best)) best = m;
    return best;
  }
  // Mean and variance of all the rewards this agent received (used when one macro-move has few).
  std::pair<double, double> pooled() const {
    double n = 0, s = 0, s2 = 0;
    for (int m = 0; m < moves(); ++m) {
      n += rewarded_[m];
      s += sum_[m];
      s2 += sum2_[m];
    }
    if (n == 0) return {0.0, 1.0};
    const double mean = s / n;
    const double var = n >= 2 ? std::max(0.0, (s2 - s * s / n) / (n - 1)) : 1.0;
    return {mean, var};
  }
  double total_pulls_ = 0;

 private:
  void scale(double f) {
    for (auto* v : {&pulls_, &rewarded_, &sum_, &sum2_})
      for (double& x : *v) x *= f;
    total_pulls_ *= f;
  }
  std::vector<double> pulls_, rewarded_, sum_, sum2_;
};

// UCB1 (Auer et al., 2002): argmax  mean(m) + c * sqrt(2 ln N / n_m). Untried macro-moves first,
// in random order. c is in the units of the reward.
class UCB1 : public BanditAgent {
 public:
  UCB1(int moves, double c) : BanditAgent(moves), c_(c) {}
  int select(Rng& rng) override {
    if (const int m = untried(rng); m >= 0) return record(m);
    const double log_n = std::log(std::max(total_pulls_, 1.0));  // >= 0, also after decay
    int best = 0;
    double best_score = -std::numeric_limits<double>::infinity();
    for (int m = 0; m < moves(); ++m) {
      const double score = mean(m) + c_ * std::sqrt(2.0 * log_n / pulls(m));
      if (score > best_score) {
        best_score = score;
        best = m;
      }
    }
    return record(best);
  }

 private:
  double c_;
};

class EpsilonGreedy : public BanditAgent {
 public:
  EpsilonGreedy(int moves, double eps) : BanditAgent(moves), eps_(eps) {}
  int select(Rng& rng) override {
    if (const int m = untried(rng); m >= 0) return record(m);
    if (rng.bernoulli(eps_)) return record(static_cast<int>(rng.below(moves())));
    return record(best_mean());
  }

 private:
  double eps_;
};

// Boltzmann exploration: P(m) proportional to exp(mean(m) / tau). tau is in the units of the reward:
// small tau = greedy, large tau = uniform.
class Softmax : public BanditAgent {
 public:
  Softmax(int moves, double tau) : BanditAgent(moves), tau_(tau), weights_(moves) {}
  int select(Rng& rng) override {
    if (const int m = untried(rng); m >= 0) return record(m);
    const double top = mean(best_mean());
    for (int m = 0; m < moves(); ++m) weights_[m] = std::exp((mean(m) - top) / tau_);
    return record(static_cast<int>(rng.weighted(weights_)));
  }

 private:
  double tau_;
  std::vector<double> weights_;
};

// Thompson sampling with a Gaussian model: for each macro-move it draws
//   mean(m) + sd(m) / sqrt(n_m) * N(0, 1)
// and chooses the largest draw. sd is estimated from the rewards themselves (pooled over all the
// macro-moves of the agent while a macro-move has fewer than two), so no parameter depends on the
// scale of the reward.
class Thompson : public BanditAgent {
 public:
  using BanditAgent::BanditAgent;
  int select(Rng& rng) override {
    if (const int m = untried(rng); m >= 0) return record(m);
    const auto [pooled_mean, pooled_var] = pooled();
    int best = 0;
    double best_draw = -std::numeric_limits<double>::infinity();
    for (int m = 0; m < moves(); ++m) {
      const double n = rewarded(m);
      const double mu = n > 0 ? mean(m) : pooled_mean;
      const double var = n >= 2 ? variance(m) : pooled_var;
      const double sd = std::sqrt(std::max(var, 1e-24) / std::max(n, 1.0));
      const double draw = mu + sd * rng.normal();
      if (draw > best_draw) {
        best_draw = draw;
        best = m;
      }
    }
    return record(best);
  }
};

// EXP3 (Auer et al., 2002), for rewards that change over time or are chosen adversarially.
// Rewards are rescaled to [0, 1] with the smallest and largest rewards this agent has seen.
// Rewards arrive late, so the probability used in the update is the current one.
class Exp3 : public BanditAgent {
 public:
  Exp3(int moves, double gamma) : BanditAgent(moves), gamma_(gamma), log_w_(moves, 0.0), p_(moves, 0.0) {}
  int select(Rng& rng) override {
    probabilities();
    return record(static_cast<int>(rng.weighted(p_)));
  }
  void update(int move, double reward) override {
    BanditAgent::update(move, reward);
    lo_ = std::min(lo_, reward);
    hi_ = std::max(hi_, reward);
    const double x = hi_ > lo_ ? (reward - lo_) / (hi_ - lo_) : 0.5;
    probabilities();
    log_w_[move] += gamma_ * x / (p_[move] * moves());
  }
  void reset() override {
    BanditAgent::reset();
    std::fill(log_w_.begin(), log_w_.end(), 0.0);
    lo_ = std::numeric_limits<double>::infinity();
    hi_ = -std::numeric_limits<double>::infinity();
  }
  void decay(double lambda) override {
    BanditAgent::decay(lambda);
    for (double& w : log_w_) w *= lambda;  // back towards uniform
  }

 private:
  void probabilities() {
    const double top = *std::max_element(log_w_.begin(), log_w_.end());
    double total = 0;
    for (int m = 0; m < moves(); ++m) total += std::exp(log_w_[m] - top);
    for (int m = 0; m < moves(); ++m) p_[m] = (1 - gamma_) * std::exp(log_w_[m] - top) / total + gamma_ / moves();
  }
  double gamma_;
  std::vector<double> log_w_, p_;
  double lo_ = std::numeric_limits<double>::infinity();
  double hi_ = -std::numeric_limits<double>::infinity();
};

// UCB1 on recent evidence only (Garivier & Moulines, 2011): the mean uses the last `window`
// rewards and the exploration term the last `window` choices, so old evidence is forgotten.
class SlidingWindowUCB : public BanditAgent {
 public:
  SlidingWindowUCB(int moves, double c, int window)
      : BanditAgent(moves), c_(c), window_(window), n_(moves, 0.0), s_(moves, 0.0), chosen_(moves, 0.0) {}
  int select(Rng& rng) override {
    int m = untried(rng);
    if (m < 0) {
      const double log_n = std::log(std::max(static_cast<double>(recent_moves_.size()), 1.0));
      double best_score = -std::numeric_limits<double>::infinity();
      m = 0;
      for (int k = 0; k < moves(); ++k) {
        const double mean_k = n_[k] > 0 ? s_[k] / n_[k] : 0.0;
        const double score = mean_k + c_ * std::sqrt(2.0 * log_n / std::max(chosen_[k], 0.5));
        if (score > best_score) {
          best_score = score;
          m = k;
        }
      }
    }
    recent_moves_.push_back(m);
    chosen_[m] += 1;
    if (static_cast<int>(recent_moves_.size()) > window_) {
      chosen_[recent_moves_.front()] -= 1;
      recent_moves_.pop_front();
    }
    return record(m);
  }
  void update(int move, double reward) override {
    BanditAgent::update(move, reward);
    recent_rewards_.emplace_back(move, reward);
    n_[move] += 1;
    s_[move] += reward;
    if (static_cast<int>(recent_rewards_.size()) > window_) {
      n_[recent_rewards_.front().first] -= 1;
      s_[recent_rewards_.front().first] -= recent_rewards_.front().second;
      recent_rewards_.pop_front();
    }
  }
  void reset() override { decay(0.0); }
  void decay(double lambda) override {  // keep the most recent fraction lambda of the window
    BanditAgent::decay(lambda);
    const auto keep = [lambda](std::size_t size) { return static_cast<std::size_t>(lambda * static_cast<double>(size)); };
    const std::size_t keep_rewards = keep(recent_rewards_.size()), keep_moves = keep(recent_moves_.size());
    while (recent_rewards_.size() > keep_rewards) {
      n_[recent_rewards_.front().first] -= 1;
      s_[recent_rewards_.front().first] -= recent_rewards_.front().second;
      recent_rewards_.pop_front();
    }
    while (recent_moves_.size() > keep_moves) {
      chosen_[recent_moves_.front()] -= 1;
      recent_moves_.pop_front();
    }
  }

 private:
  double c_;
  int window_;
  std::vector<double> n_, s_, chosen_;
  std::deque<std::pair<int, double>> recent_rewards_;
  std::deque<int> recent_moves_;
};

// UCB1 with discounted statistics (Kocsis & Szepesvári, 2006): every new reward multiplies the old
// rewards by gamma, and every choice multiplies the old choices by gamma.
class DiscountedUCB : public BanditAgent {
 public:
  DiscountedUCB(int moves, double c, double gamma)
      : BanditAgent(moves), c_(c), gamma_(gamma), n_(moves, 0.0), s_(moves, 0.0), chosen_(moves, 0.0) {}
  int select(Rng& rng) override {
    int m = untried(rng);
    if (m < 0) {
      double total = 0;
      for (double x : chosen_) total += x;
      const double log_n = std::log(std::max(total, 1.0));
      double best_score = -std::numeric_limits<double>::infinity();
      m = 0;
      for (int k = 0; k < moves(); ++k) {
        const double mean_k = n_[k] > 1e-12 ? s_[k] / n_[k] : 0.0;
        const double score = mean_k + c_ * std::sqrt(2.0 * log_n / std::max(chosen_[k], 1e-3));
        if (score > best_score) {
          best_score = score;
          m = k;
        }
      }
    }
    for (double& x : chosen_) x *= gamma_;
    chosen_[m] += 1;
    return record(m);
  }
  void update(int move, double reward) override {
    BanditAgent::update(move, reward);
    for (int k = 0; k < moves(); ++k) {
      n_[k] *= gamma_;
      s_[k] *= gamma_;
    }
    n_[move] += 1;
    s_[move] += reward;
  }
  void reset() override { decay(0.0); }
  void decay(double lambda) override {
    BanditAgent::decay(lambda);
    for (auto* v : {&n_, &s_, &chosen_})
      for (double& x : *v) x *= lambda;
  }

 private:
  double c_, gamma_;
  std::vector<double> n_, s_, chosen_;
};

// Gradient bandit (Sutton & Barto, 2018, Section 2.8): learns a preference H(m) per macro-move and
// chooses with probability softmax(H). Each reward is compared with the running average of the
// rewards and divided by their running standard deviation, so alpha does not depend on the scale.
class GradientBandit : public BanditAgent {
 public:
  GradientBandit(int moves, double alpha) : BanditAgent(moves), alpha_(alpha), h_(moves, 0.0), p_(moves, 0.0) {}
  int select(Rng& rng) override {
    probabilities();
    return record(static_cast<int>(rng.weighted(p_)));
  }
  void update(int move, double reward) override {
    BanditAgent::update(move, reward);
    const double sd = count_ >= 2 ? std::sqrt(m2_ / (count_ - 1)) : 1.0;
    const double advantage = (reward - average_) / std::max(sd, 1e-12);
    probabilities();
    for (int m = 0; m < moves(); ++m) h_[m] += alpha_ * advantage * ((m == move ? 1.0 : 0.0) - p_[m]);
    // running mean and variance (Welford)
    count_ += 1;
    const double delta = reward - average_;
    average_ += delta / count_;
    m2_ += delta * (reward - average_);
  }
  void reset() override { decay(0.0); }
  void decay(double lambda) override {
    BanditAgent::decay(lambda);
    for (double& h : h_) h *= lambda;
    count_ *= lambda;
    m2_ *= lambda;
    if (count_ < 1e-12) average_ = 0;
  }

 private:
  void probabilities() {
    const double top = *std::max_element(h_.begin(), h_.end());
    double total = 0;
    for (int m = 0; m < moves(); ++m) total += (p_[m] = std::exp(h_[m] - top));
    for (double& p : p_) p /= total;
  }
  double alpha_;
  std::vector<double> h_, p_;
  double count_ = 0, average_ = 0, m2_ = 0;
};

class RandomAgent : public BanditAgent {
 public:
  using BanditAgent::BanditAgent;
  int select(Rng& rng) override { return record(static_cast<int>(rng.below(moves()))); }
};

class FixedAgent : public BanditAgent {
 public:
  FixedAgent(int moves, int move) : BanditAgent(moves), move_(move) {}
  int select(Rng&) override { return record(move_); }

 private:
  int move_;
};

// ---------------------------------------------------------------------------------------------
// Registry: agent.type text -> agent
// ---------------------------------------------------------------------------------------------
using AgentFactory =
    std::function<std::unique_ptr<Agent>(const Spec& spec, const std::vector<std::string>& move_names)>;

namespace detail {

inline int move_index(const Spec& spec, const std::vector<std::string>& names) {
  const std::string m = spec.text("move", "");
  for (std::size_t i = 0; i < names.size(); ++i)
    if (names[i] == m) return static_cast<int>(i);
  try {
    const int i = from_text<int>("fixed(move)", m);
    if (i >= 0 && i < static_cast<int>(names.size())) return i;
  } catch (const ConfigError&) {
  }
  std::string list;
  for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
  throw ConfigError("fixed(move=" + m + "): no macro-move with that name or index. Macro-moves: " + list);
}

// A numeric argument with a range check: "softmax(tau)" must be > 0, etc.
inline double argument(const Spec& s, const char* key, double fallback, double low, double high, bool open_low) {
  const double v = s.number(key, fallback);
  if (v < low || v > high || (open_low && v == low))
    throw ConfigError(s.name + "(" + key + "=" + format_number(v) + "): must be in " + (open_low ? "(" : "[") +
                      format_number(low) + ", " + format_number(high) + "]");
  return v;
}

template <class A, class... Args>
std::unique_ptr<Agent> make(Args... args) {
  return std::unique_ptr<Agent>(new A(args...));
}

inline std::map<std::string, AgentFactory>& agent_registry() {
  static constexpr double inf = std::numeric_limits<double>::infinity();
  using Names = std::vector<std::string>;
  static std::map<std::string, AgentFactory> registry = {
      {"ucb1",
       [](const Spec& s, const Names& n) {
         s.allow_only({"c"});
         return make<UCB1>(int(n.size()), argument(s, "c", 1.0, 0, inf, false));
       }},
      {"epsilon_greedy",
       [](const Spec& s, const Names& n) {
         s.allow_only({"eps"});
         return make<EpsilonGreedy>(int(n.size()), argument(s, "eps", 0.1, 0, 1, false));
       }},
      {"softmax",
       [](const Spec& s, const Names& n) {
         s.allow_only({"tau"});
         return make<Softmax>(int(n.size()), argument(s, "tau", 0.1, 0, inf, true));
       }},
      {"thompson",
       [](const Spec& s, const Names& n) {
         s.allow_only({});
         return make<Thompson>(int(n.size()));
       }},
      {"exp3",
       [](const Spec& s, const Names& n) {
         s.allow_only({"gamma"});
         return make<Exp3>(int(n.size()), argument(s, "gamma", 0.1, 0, 1, true));
       }},
      {"sliding_window_ucb",
       [](const Spec& s, const Names& n) {
         s.allow_only({"c", "window"});
         return make<SlidingWindowUCB>(int(n.size()), argument(s, "c", 0.5, 0, inf, false),
                                       int(argument(s, "window", 300, 1, inf, false)));
       }},
      {"discounted_ucb",
       [](const Spec& s, const Names& n) {
         s.allow_only({"c", "gamma"});
         return make<DiscountedUCB>(int(n.size()), argument(s, "c", 0.5, 0, inf, false),
                                    argument(s, "gamma", 0.99, 0, 1, true));
       }},
      {"gradient",
       [](const Spec& s, const Names& n) {
         s.allow_only({"alpha"});
         return make<GradientBandit>(int(n.size()), argument(s, "alpha", 0.1, 0, inf, true));
       }},
      {"random",
       [](const Spec& s, const Names& n) {
         s.allow_only({});
         return make<RandomAgent>(int(n.size()));
       }},
      {"fixed",
       [](const Spec& s, const Names& n) {
         s.allow_only({"move"});
         return make<FixedAgent>(int(n.size()), move_index(s, n));
       }},
  };
  return registry;
}

}  // namespace detail

// Adds your own agent: gso::register_agent("my_agent", [](const gso::Spec& s, const auto& moves) {...});
inline void register_agent(const std::string& name, AgentFactory factory) {
  detail::agent_registry()[name] = std::move(factory);
}

inline std::unique_ptr<Agent> make_agent(const Spec& spec, const std::vector<std::string>& move_names) {
  auto& registry = detail::agent_registry();
  const auto it = registry.find(spec.name);
  if (it == registry.end()) {
    std::string list;
    for (const auto& kv : registry) list += (list.empty() ? "" : ", ") + kv.first;
    throw ConfigError("agent.type: unknown agent '" + spec.name + "'. Available: " + list);
  }
  return it->second(spec, move_names);
}

}  // namespace gso
