#include "Engine/alpha_engine.hpp"
// std
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
     std::cout << "Starting program..." << std::endl;
     AlphaEngine engine{};
    try {
        engine.run();
        std::cout << "Engine run completed normally" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Main exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "Unknown exception in main" << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Program ending... Press Enter to exit." << std::endl;
    std::cin.get();
    return EXIT_SUCCESS;
}