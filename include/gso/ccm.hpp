#pragma once
// Current Context Memory (CCM): decisions waiting for their reward (docs/02-framework.md, Section 2.6).
//
// A FIFO of fixed capacity ccm.size. When a decision arrives and the memory is full, the oldest
// one leaves and is rewarded with what happened since it was taken. ccm.size = 0 rewards every
// decision right after its own macro-move.

#include <cstdint>
#include <vector>

namespace gso {

// One decision, as it was when it was taken.
struct Decision {
  std::uint64_t guide_id;    // identity of the guide of its region (contextual validation)
  int slot;                  // guide slot and ring whose agent took it
  int ring;
  int move;                  // macro-move chosen
  double cost_before;        // f(S) when it was taken
  double best_before;        // f(S*) when it was taken
  double guide_cost_before;  // cost of that guide when it was taken
  int agent = 0;             // the agent that took it, which receives its reward (GCM::agent_index)
};

class CCM {
 public:
  explicit CCM(int capacity) : capacity_(capacity), buffer_(capacity > 0 ? capacity : 1) {}

  // Registers a new decision; `settle(decision)` is called for every decision that is due.
  template <class Settle>
  void push(const Decision& d, Settle&& settle) {
    if (capacity_ == 0) {
      settle(d);
      return;
    }
    if (count_ == capacity_) pop(settle);
    buffer_[(head_ + count_) % capacity_] = d;
    ++count_;
  }

  // Settles every pending decision, oldest first, and empties the memory.
  template <class Settle>
  void flush(Settle&& settle) {
    while (count_ > 0) pop(settle);
  }

  int pending() const { return count_; }

 private:
  template <class Settle>
  void pop(Settle& settle) {
    const Decision d = buffer_[head_];
    head_ = (head_ + 1) % capacity_;
    --count_;
    settle(d);
  }

  int capacity_;
  std::vector<Decision> buffer_;  // circular buffer: no allocation after construction
  int head_ = 0;
  int count_ = 0;
};

}  // namespace gso
