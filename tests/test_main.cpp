#include "test.hpp"
#include <iostream>
namespace test {
std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}
} // namespace test
int main() {
    size_t failed = 0;
    for (auto& t : test::cases()) {
        try {
            t.run();
            std::cout << "PASS " << t.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << t.name << ": " << e.what() << '\n';
        }
    }
    std::cout << test::cases().size() - failed << '/' << test::cases().size() << " tests passed\n";
    return failed ? 1 : 0;
}
