#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <iostream>

namespace ANSI_Colors
{
    constexpr const char* MAYBE_RED = "\033[33m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* VERY_RED = "\033[30;101m";
    constexpr const char* RESET = "\033[0m";
}

inline void logMsg(const char* message)
{
    std::cerr << "LOG: " << message << "\n";
}

inline void logMsg(const std::string& message)
{
    logMsg(message.c_str());
}

inline void logWarning(const char* message)
{
    std::cerr << ANSI_Colors::MAYBE_RED << "WARNING: "
        << message << ANSI_Colors::RESET << "\n";
}

inline void logWarning(const std::string& message)
{
    logWarning(message.c_str());
}

inline void logError(const char* message)
{
    std::cerr << ANSI_Colors::RED << "ERROR: " <<
        message << ANSI_Colors::RESET << "\n";
}

inline void logError(const std::string& message)
{
    logError(message.c_str());
}

inline void logCaughtException(const char* message)
{
    std::cerr << ANSI_Colors::VERY_RED << "EXCEPTION CAUGHT: "
        << message << ANSI_Colors::RESET << "\n";
}

inline void logCaughtException(const std::string& message)
{
    logCaughtException(message.c_str());
}

#endif // !DEBUG_H