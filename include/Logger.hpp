#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

class Logger {
public:
    explicit Logger(std::string logFilePath = "logs/mini-db.log");

    void info(const std::string& message);
    void error(const std::string& message);
    void perf(const std::string& message);

private:
    void write(const std::string& level, const std::string& message);
    std::string timestamp() const;

    std::string logFilePath;
};

#endif
