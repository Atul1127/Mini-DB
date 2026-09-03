#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

Logger::Logger(std::string logFilePath)
    : logFilePath(std::move(logFilePath)) {
    const std::filesystem::path path(this->logFilePath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
}

void Logger::info(const std::string& message) {
    write("INFO", message);
}

void Logger::error(const std::string& message) {
    write("ERROR", message);
}

void Logger::perf(const std::string& message) {
    write("PERF", message);
}

void Logger::write(const std::string& level, const std::string& message) {
    std::ofstream out(logFilePath, std::ios::app);
    if (!out) {
        return;
    }
    out << '[' << timestamp() << "] [" << level << "] " << message << '\n';
}

std::string Logger::timestamp() const {
    const auto now = std::chrono::system_clock::now();
    const auto timePointSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto microsecondsPart = std::chrono::duration_cast<std::chrono::microseconds>(
                                      now - timePointSeconds)
                                      .count();

    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &currentTime);
#else
    localtime_r(&currentTime, &localTime);
#endif

    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(6) << std::setfill('0') << microsecondsPart;
    return out.str();
}
