#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace test {
struct Case {
    const char* name;
    std::function<void()> run;
};
std::vector<Case>& cases();
struct Register {
    Register(const char* n, std::function<void()> f) {
        cases().push_back({n, f});
    }
};
inline void check(bool ok, const char* expr, const char* file, int line) {
    if (!ok)
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expr);
}
template <class F> void throws(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception&) {
        caught = true;
    }
    if (!caught)
        throw std::runtime_error("expected exception");
}
} // namespace test
#define TEST(name)                                                                                 \
    static void name();                                                                            \
    static test::Register reg_##name(#name, name);                                                 \
    static void name()
#define CHECK(expr) test::check(bool(expr), #expr, __FILE__, __LINE__)
#define THROWS(expr) test::throws([&] { (void)(expr); })
