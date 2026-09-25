#pragma once
#include "flow.hpp"
#include <stdexcept>
namespace pf {
inline size_t rss_worker(const FlowKey& f, size_t workers) {
    if (!workers)
        throw std::invalid_argument("zero workers");
    return FlowHash{}(f) % workers;
}
} // namespace pf
