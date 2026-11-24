#pragma once
// The problem adapter: everything GSO-RL needs to know about your problem
// (docs/03-adapting-to-your-problem.md).
//
//   struct MyProblem {
//     using Solution = MySolution;                                   // any copyable type
//     double   cost(const Solution& s) const;                         // minimized
//     Solution make_guide(gso::Rng& rng);                             // builds one guide
//     double   distance(const Solution& a, const Solution& b) const;  // d(a,a) = 0, d >= 0
//     gso::MacroMoves<MyProblem> macro_moves();                       // the agents' actions
//
//     // Optional:
//     double distance(const Solution& a, const Solution& b, double limit) const;
//         // may stop early and return any value > limit once the distance exceeds limit
//     double max_distance() const;
//         // largest possible distance, for region.delta_ratio with region.delta_reference = adapter
//     void parameters(gso::Params& p);
//         // your own parameters: p.add("tabu_steps", tabu_steps, "help"); -> problem.tabu_steps
//   };

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "params.hpp"
#include "rng.hpp"

namespace gso {

template <class P>
class Context;  // defined in solver.hpp

// Marks a macro-move that produces a solution unrelated to the current one (for example, a
// construction from scratch). Before it is applied, the Current Context Memory is flushed.
struct RestartTag {
  explicit constexpr RestartTag() = default;
};
inline constexpr RestartTag restart{};

// One macro-move: a name and a procedure that transforms the current solution.
//   {"name", &MyProblem::method}                    void method(Solution& s, gso::Context<MyProblem>& ctx)
//   {"name", &MyProblem::method, gso::restart}
//   {"name", [](Solution& s, gso::Context<MyProblem>& ctx) { ... }}
template <class P>
struct MacroMove {
  using Solution = typename P::Solution;
  using Procedure = std::function<void(P&, Solution&, Context<P>&)>;

  std::string name;
  Procedure apply;
  bool is_restart = false;

  template <class F>
  MacroMove(std::string move_name, F&& procedure)
      : name(std::move(move_name)), apply(wrap(std::forward<F>(procedure))) {}

  template <class F>
  MacroMove(std::string move_name, F&& procedure, RestartTag)
      : name(std::move(move_name)), apply(wrap(std::forward<F>(procedure))), is_restart(true) {}

 private:
  template <class F>
  static Procedure wrap(F&& f) {
    if constexpr (std::is_invocable_v<F&, P&, Solution&, Context<P>&>) {
      return Procedure(std::forward<F>(f));  // member function: &MyProblem::method
    } else {
      static_assert(std::is_invocable_v<F&, Solution&, Context<P>&>,
                    "GSO-RL: a macro-move must be a member function `void name(Solution& s, gso::Context<P>& ctx)` "
                    "or a callable taking (Solution&, gso::Context<P>&)");
      return [g = std::forward<F>(f)](P&, Solution& s, Context<P>& ctx) mutable { g(s, ctx); };
    }
  }
};

template <class P>
using MacroMoves = std::vector<MacroMove<P>>;

// ---------------------------------------------------------------------------------------------
// Compile-time checks of the adapter, with messages that say what is missing.
// ---------------------------------------------------------------------------------------------
namespace detail {

template <class P, class = void>
struct has_solution_type : std::false_type {};
template <class P>
struct has_solution_type<P, std::void_t<typename P::Solution>> : std::true_type {};

template <class P, class S, class = void>
struct has_cost : std::false_type {};
template <class P, class S>
struct has_cost<P, S, std::void_t<decltype(double(std::declval<P&>().cost(std::declval<const S&>())))>>
    : std::true_type {};

template <class P, class S, class = void>
struct has_make_guide : std::false_type {};
template <class P, class S>
struct has_make_guide<P, S, std::void_t<decltype(std::declval<P&>().make_guide(std::declval<Rng&>()))>>
    : std::is_convertible<decltype(std::declval<P&>().make_guide(std::declval<Rng&>())), S> {};

template <class P, class S, class = void>
struct has_distance : std::false_type {};
template <class P, class S>
struct has_distance<P, S,
                    std::void_t<decltype(double(std::declval<P&>().distance(std::declval<const S&>(),
                                                                           std::declval<const S&>())))>>
    : std::true_type {};

template <class P, class S, class = void>
struct has_bounded_distance : std::false_type {};
template <class P, class S>
struct has_bounded_distance<P, S,
                            std::void_t<decltype(double(std::declval<P&>().distance(
                                std::declval<const S&>(), std::declval<const S&>(), 0.0)))>> : std::true_type {};

template <class P, class = void>
struct has_max_distance : std::false_type {};
template <class P>
struct has_max_distance<P, std::void_t<decltype(double(std::declval<P&>().max_distance()))>> : std::true_type {};

template <class P, class = void>
struct has_macro_moves : std::false_type {};
template <class P>
struct has_macro_moves<P, std::void_t<decltype(std::declval<P&>().macro_moves())>>
    : std::is_convertible<decltype(std::declval<P&>().macro_moves()), MacroMoves<P>> {};

template <class P, class = void>
struct has_parameters : std::false_type {};
template <class P>
struct has_parameters<P, std::void_t<decltype(std::declval<P&>().parameters(std::declval<Params&>()))>>
    : std::true_type {};

// Compile-time checks; gso::check_adapter (check.hpp) checks the behaviour at run time.
template <class P>
constexpr bool check_adapter_types() {
  static_assert(detail::has_solution_type<P>::value,
                "GSO-RL adapter: declare `using Solution = <your solution type>;` inside the adapter struct");
  if constexpr (detail::has_solution_type<P>::value) {
    using S = typename P::Solution;
    static_assert(std::is_copy_constructible_v<S> && std::is_copy_assignable_v<S>,
                  "GSO-RL adapter: the Solution type must be copyable (the memory stores copies of guides)");
    static_assert(detail::has_cost<P, S>::value, "GSO-RL adapter: missing `double cost(const Solution& s) const`");
    static_assert(detail::has_make_guide<P, S>::value, "GSO-RL adapter: missing `Solution make_guide(gso::Rng& rng)`");
    static_assert(detail::has_distance<P, S>::value,
                  "GSO-RL adapter: missing `double distance(const Solution& a, const Solution& b) const`");
    static_assert(detail::has_macro_moves<P>::value,
                  "GSO-RL adapter: missing `gso::MacroMoves<YourAdapter> macro_moves()`");
  }
  return true;
}

}  // namespace detail

// Registers the adapter's own parameters (if it has any) under the prefix "problem.".
template <class P>
void bind_problem_parameters(P& problem, Params& p) {
  if constexpr (detail::has_parameters<P>::value) {
    p.prefix("problem.");
    problem.parameters(p);
    p.prefix("");
  }
}

}  // namespace gso
