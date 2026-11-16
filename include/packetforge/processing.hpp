#pragma once
#include "flow.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>
namespace pf {
enum class Action { Allow, Drop };
struct Rule {
    Action action = Action::Allow;
    std::optional<uint32_t> source, destination;
    std::optional<uint16_t> source_port, destination_port;
    std::optional<uint8_t> protocol;
    bool matches(const FlowKey& k) const {
        return (!source || *source == k.source) && (!destination || *destination == k.destination) &&
               (!source_port || *source_port == k.source_port) &&
               (!destination_port || *destination_port == k.destination_port) &&
               (!protocol || *protocol == k.protocol);
    }
};
// First matching line wins; no match allows. Rules are copied before threads start.
std::vector<Rule> load_rules(const std::string& path);
struct FlowEntry {
    FlowKey key{};
    uint64_t packets = 0, bytes = 0, last_seen = 0;
    bool occupied = false;
};
class FlowTable {
    std::vector<FlowEntry> entries_;
    uint64_t sequence_ = 0, evictions_ = 0, collisions_ = 0;
    size_t active_ = 0;
  public:
    explicit FlowTable(size_t capacity = 0) : entries_(checked(capacity)) {}
    static size_t checked(size_t n) {
        if (n > 1048576 || (n && n % 4))
            throw std::invalid_argument("flow capacity must be 0 or a multiple of 4, at most 1048576 per worker");
        return n;
    }
    // Four-way set associative LRU: bounded four probes, no packet-time allocations.
    void observe(const FlowKey& key, size_t bytes) {
        if (entries_.empty()) return;
        const size_t first = (FlowHash{}(key) % (entries_.size() / 4)) * 4;
        FlowEntry* victim = &entries_[first];
        ++sequence_;
        for (size_t i = first; i < first + 4; ++i) {
            auto& e = entries_[i];
            if (e.occupied && e.key == key) {
                ++e.packets; e.bytes += bytes; e.last_seen = sequence_; return;
            }
            if (!e.occupied || (victim->occupied && e.last_seen < victim->last_seen)) victim = &e;
        }
        if (victim->occupied) { ++evictions_; ++collisions_; }
        else ++active_;
        *victim = {key, 1, bytes, sequence_, true};
    }
    const std::vector<FlowEntry>& entries() const { return entries_; }
    size_t active() const { return active_; }
    uint64_t evictions() const { return evictions_; }
    uint64_t collisions() const { return collisions_; }
};
class Processor {
    const std::vector<Rule> rules_;
  public:
    FlowTable flows;
    Processor(const std::vector<Rule>& rules = {}, size_t capacity = 0) : rules_(rules), flows(capacity) {}
    bool accept(const pf_packet& packet, size_t bytes) {
        const auto key = flow_key(packet);
        // Count valid packets seen by policy, including policy rejects, before TX outcomes.
        flows.observe(key, bytes);
        for (const auto& rule : rules_) if (rule.matches(key)) return rule.action == Action::Allow;
        return true;
    }
};
// Sink failure is an accounted drop. Implementations must not throw or retain the buffer.
class FrameSink {
  public:
    virtual ~FrameSink() = default;
    virtual bool write(const uint8_t* data, size_t length) noexcept = 0;
};
} // namespace pf
