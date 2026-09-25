#include "benchmark_common.hpp"
int main(int argc, char** argv) {
    try {
        auto n = pf::benchmark_count(argc, argv);
        pf::csv_header(std::cout);
        pf::benchmark(true, 4, n);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
