#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <string>
#include <variant>

#include "Database.hpp"
#include "Logger.hpp"
#include "QueryParser.hpp"

namespace {

std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

}  // namespace

int main() {
    Logger logger("logs/mini-db.log");
    Database database;
    std::string loadError;
    if (!database.loadFromDisk("data", loadError)) {
        logger.error("Startup load failed: " + loadError);
        std::cerr << "Warning: failed to load persisted data: " << loadError << '\n';
    } else {
        logger.info("Startup load completed.");
    }
    QueryParser parser(database);

    std::cout << "Mini DB CLI started. Type EXIT to quit." << '\n';

    std::string input;
    while (true) {
        std::cout << "mini-db> ";
        if (!std::getline(std::cin, input)) {
            break;
        }

        const std::string normalized = toUpper(trim(input));
        if (normalized == "EXIT") {
            std::cout << "Exiting mini-db." << '\n';
            break;
        }

        if (normalized.empty()) {
            continue;
        }

        logger.info("Query: " + input);
        const auto start = std::chrono::high_resolution_clock::now();
        const QueryResult result = parser.execute(input);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << result.message << '\n';
        if (result.success) {
            logger.info("Success: " + result.message);
        } else {
            logger.error("Failure: " + result.message);
        }
        logger.perf("Execution time: " + std::to_string(elapsedUs) + " us");

        if (result.success && !result.rows.empty()) {
            for (std::size_t rowIndex = 0; rowIndex < result.rows.size(); ++rowIndex) {
                std::cout << "  row[" << rowIndex << "]: ";
                const auto& values = result.rows[rowIndex].getValues();
                for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                    std::visit([](const auto& value) { std::cout << value; }, values[valueIndex]);
                    if (valueIndex + 1 < values.size()) {
                        std::cout << " | ";
                    }
                }
                std::cout << '\n';
            }
        }
    }

    return 0;
}
