#include "packetforge/processing.hpp"
#include <fstream>
#include <sstream>
namespace pf {
namespace {
uint32_t integer(const std::string& s, uint32_t max) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned decimal: " + s);
    const auto n = std::stoull(s);
    if (n > max) throw std::invalid_argument("rule value out of range: " + s);
    return static_cast<uint32_t>(n);
}
uint32_t address(const std::string& s) {
    std::istringstream input(s);
    std::string part;
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        if (!std::getline(input, part, '.')) throw std::invalid_argument("invalid IPv4 address: " + s);
        result = (result << 8) | integer(part, 255);
    }
    if (std::getline(input, part, '.') || s.back() == '.') throw std::invalid_argument("invalid IPv4 address: " + s);
    return result;
}
}
std::vector<Rule> load_rules(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::invalid_argument("cannot open rules: " + path);
    std::vector<Rule> rules;
    std::string line;
    size_t ordinal = 0;
    while (std::getline(input, line)) {
        ++ordinal;
        line = line.substr(0, line.find('#'));
        std::istringstream fields(line);
        std::string action, src, dst, sport, dport, proto, extra;
        if (!(fields >> action)) continue;
        try {
            if (!(fields >> src >> dst >> sport >> dport >> proto) || fields >> extra)
                throw std::invalid_argument("expected ACTION SRC DST SPORT DPORT PROTO");
            Rule r;
            if (action == "ALLOW") r.action = Action::Allow;
            else if (action == "DROP") r.action = Action::Drop;
            else throw std::invalid_argument("action must be ALLOW or DROP");
            if (src != "*") r.source = address(src);
            if (dst != "*") r.destination = address(dst);
            if (sport != "*") r.source_port = static_cast<uint16_t>(integer(sport, 65535));
            if (dport != "*") r.destination_port = static_cast<uint16_t>(integer(dport, 65535));
            if (proto != "*") r.protocol = static_cast<uint8_t>(integer(proto, 255));
            if (rules.size() == 1024) throw std::invalid_argument("at most 1024 rules");
            rules.push_back(r);
        } catch (const std::exception& e) {
            throw std::invalid_argument(path + ":" + std::to_string(ordinal) + ": " + e.what());
        }
    }
    if (input.bad()) throw std::runtime_error("rules read failed: " + path);
    return rules;
}
} // namespace pf
