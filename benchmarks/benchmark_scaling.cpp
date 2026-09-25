#include "benchmark_common.hpp"
int main(int argc, char** argv) {
    try {
        auto n = pf::benchmark_count(argc, argv);
        pf::csv_header(std::cout);
        for (size_t w : {1u, 2u, 4u, 8u}) {
            pf::benchmark(true, w, n);
            pf::benchmark(false, w, n);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
