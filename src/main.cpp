#include "Engine/alpha_engine.hpp"
#include "Engine/log.hpp"
// std
#include <cstdlib>
#include <stdexcept>

int main() {
    AlphaEngine engine{};
    try {
        engine.run();
    } catch (const std::exception& e) {
        Log::error("Main exception: ", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        Log::error("Unknown exception in main");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}