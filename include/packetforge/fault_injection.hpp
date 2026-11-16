#pragma once
#include <cstddef>
namespace pf {
// Deterministic device-side rejection, separate from naturally occurring pressure.
struct FaultInjection {
    size_t reject_rx_every = 0, reject_tx_every = 0;
    size_t fail_start_worker = 0; // One-based startup failure injection; zero disables.
};
inline bool reject_nth(size_t ordinal, size_t period) {
    return period && ordinal % period == 0;
}
} // namespace pf
