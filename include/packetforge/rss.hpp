#pragma once
#include "flow.hpp"
#include <stdexcept>
namespace pf {
inline size_t rss_worker(const FlowKey& f, size_t workers) {
    if (!workers)
        throw std::invalid_argument("zero workers");
    return FlowHash{}(f) % workers; // hash then modulo
}
} // namespace pf
//rss : reciver side scaling , this is not real hardware  RSS , here we make the same concept in software

