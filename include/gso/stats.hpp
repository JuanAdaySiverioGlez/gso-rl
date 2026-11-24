#pragma once
// What happened during a run: the same measures the article reports in Section 5.5.4,
// plus where the time went. They only count; they never influence the search.

#include <array>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

namespace gso {

// One improvement of the best solution.
struct TracePoint {
  double seconds;
  long long iteration;
  double cost;
};

struct Stats {
  std::vector<std::string> moves;      // macro-move names, in the adapter's order
  std::vector<bool> restart;           // which of them are marked restart
  int rings = 0;
  double delta_max = 0;                // radius of the regions used in the run

  long long iterations = 0;            // global iterations (regional searches) completed
  long long macro_moves = 0;           // macro-moves applied
  std::array<long long, 5> cases{};    // GCM update cases A..E
  std::vector<long long> chosen;       // times each macro-move was chosen
  std::vector<long long> chosen_by_ring;  // [ring * moves + move]
  long long rewards_paid = 0;
  long long rewards_discarded = 0;     // discarded by contextual validation
  double reward_abs_sum = 0;           // sum of |reward| of the paid rewards (scale of the rewards)

  // Saturation (region.saturation): a guide is saturated after that many regional searches in a row
  // from it that did not improve it. What the search does there shows whether it leaves exhausted regions.
  int saturation = 0;                     // region.saturation of the run
  long long searches_saturated = 0;       // regional searches started from a saturated guide
  std::vector<long long> chosen_saturated;  // macro-moves chosen in those searches
  long long longest_streak = 0;           // most regional searches in a row without improving one guide
  long long saturated_replaced = 0;       // regions abandoned (region.on_saturation = replace or its controls)

  double seconds = 0;                  // whole run
  double seconds_building_guides = 0;  // initialization of the GCM
  double seconds_in_moves = 0;         // inside the adapter's macro-moves

  double mean_abs_reward() const { return rewards_paid > 0 ? reward_abs_sum / static_cast<double>(rewards_paid) : 0.0; }

  // Share of the run spent in the GSO-RL core (memory, agents, rewards, distances).
  double core_share() const {
    return seconds > 0 ? (seconds - seconds_in_moves - seconds_building_guides) / seconds : 0.0;
  }

  void print(std::ostream& os) const {
    auto pct = [](double part, double whole) { return whole > 0 ? 100.0 * part / whole : 0.0; };
    os << std::fixed << std::setprecision(1);
    os << "  iterations: " << iterations << ", macro-moves: " << macro_moves << ", time: " << seconds << " s"
       << ", region.delta_max: " << std::defaultfloat << std::setprecision(4) << delta_max << std::fixed
       << std::setprecision(1) << "\n";
    long long total_cases = 0;
    for (long long c : cases) total_cases += c;
    os << "  GCM update cases:";
    for (int c = 0; c < 5; ++c) os << "  " << char('A' + c) << " " << pct(double(cases[c]), double(total_cases)) << "%";
    os << "\n  macro-moves chosen:";
    for (std::size_t m = 0; m < moves.size(); ++m)
      os << "  " << moves[m] << " " << pct(double(chosen[m]), double(macro_moves)) << "%";
    const long long rewards = rewards_paid + rewards_discarded;
    os << "\n  rewards: " << rewards_paid << " paid, " << rewards_discarded
       << " discarded by contextual validation (" << pct(double(rewards_discarded), double(rewards))
       << "%), mean |reward| " << std::defaultfloat << std::setprecision(3) << mean_abs_reward()
       << std::fixed << std::setprecision(1) << "\n";
    long long in_saturated = 0;
    for (long long c : chosen_saturated) in_saturated += c;
    os << "  saturated regions (" << saturation << "+ searches without improving): "
       << pct(double(searches_saturated), double(iterations)) << "% of the regional searches, longest streak "
       << longest_streak;
    if (saturated_replaced > 0) os << ", " << saturated_replaced << " regions abandoned for new guides";
    if (in_saturated > 0) {
      os << "; macro-moves chosen there:";
      for (std::size_t m = 0; m < moves.size(); ++m)
        os << "  " << moves[m] << " " << pct(double(chosen_saturated[m]), double(in_saturated)) << "%";
    }
    os << "\n";
    os << "  time: " << pct(seconds_in_moves, seconds) << "% in macro-moves, "
       << pct(seconds_building_guides, seconds) << "% building guides, " << 100.0 * core_share()
       << "% in the GSO-RL core\n";
    if (core_share() > 0.2 && seconds > 1)
      os << "  note: the core includes cost() and distance() to every guide after every macro-move; if that is\n"
            "        expensive, provide the bounded distance(a, b, limit) (docs/03) or make the macro-moves longer\n";
    os << std::defaultfloat << std::setprecision(6);
  }
};

}  // namespace gso
