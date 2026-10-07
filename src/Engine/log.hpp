#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>

// Minimal leveled logger. Info goes to stdout, warn and error to stderr.
// Threshold: environment variable ALPHA_LOG = debug | info | warn | error (default info).
//   Log::info("Loaded ", count, " meshes");
namespace Log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

inline Level threshold() {
    static const Level level = [] {
        const char* value = std::getenv("ALPHA_LOG");
        if (value == nullptr) {
            return Level::Info;
        }
        if (std::strcmp(value, "debug") == 0) {
            return Level::Debug;
        }
        if (std::strcmp(value, "warn") == 0) {
            return Level::Warn;
        }
        if (std::strcmp(value, "error") == 0) {
            return Level::Error;
        }
        return Level::Info;
    }();
    return level;
}

template <typename... Args> void write(Level level, const char* prefix, Args&&... args) {
    if (level < threshold()) {
        return;
    }
    std::ostringstream line;
    line << prefix;
    (line << ... << std::forward<Args>(args));
    line << '\n';
    std::ostream& out = level >= Level::Warn ? std::cerr : std::cout;
    out << line.str() << std::flush;
}

template <typename... Args> void debug(Args&&... args) {
    write(Level::Debug, "", std::forward<Args>(args)...);
}
template <typename... Args> void info(Args&&... args) {
    write(Level::Info, "", std::forward<Args>(args)...);
}
template <typename... Args> void warn(Args&&... args) {
    write(Level::Warn, "warning: ", std::forward<Args>(args)...);
}
template <typename... Args> void error(Args&&... args) {
    write(Level::Error, "error: ", std::forward<Args>(args)...);
}

} // namespace Log
