#include "Database.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <variant>

namespace {

std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INT:
            return "INT";
        case DataType::FLOAT:
            return "FLOAT";
        case DataType::STRING:
            return "STRING";
    }
    return "STRING";
}

bool parseDataType(const std::string& rawType, DataType& outType) {
    if (rawType == "INT") {
        outType = DataType::INT;
        return true;
    }
    if (rawType == "FLOAT") {
        outType = DataType::FLOAT;
        return true;
    }
    if (rawType == "STRING") {
        outType = DataType::STRING;
        return true;
    }
    return false;
}

std::string escapeString(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char ch : input) {
        if (ch == '\\' || ch == '"') {
            output.push_back('\\');
        }
        output.push_back(ch);
    }
    return output;
}

std::string serializeValue(const Value& value) {
    if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    }
    if (std::holds_alternative<float>(value)) {
        std::ostringstream out;
        out << std::get<float>(value);
        return out.str();
    }
    return "\"" + escapeString(std::get<std::string>(value)) + "\"";
}

bool splitCsvLine(const std::string& line, std::vector<std::string>& outTokens, std::string& outError) {
    outTokens.clear();
    std::string current;
    bool inQuotes = false;
    bool escaped = false;

    for (char ch : line) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inQuotes = !inQuotes;
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && !inQuotes) {
            outTokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (escaped || inQuotes) {
        outError = "Malformed CSV line: unmatched escape or quote.";
        return false;
    }

    outTokens.push_back(current);
    return true;
}

bool parseValueForType(const std::string& token, DataType type, Value& outValue, std::string& outError) {
    try {
        switch (type) {
            case DataType::INT: {
                std::size_t pos = 0;
                const int parsed = std::stoi(token, &pos);
                if (pos != token.size()) {
                    outError = "Invalid INT token '" + token + "'.";
                    return false;
                }
                outValue = parsed;
                return true;
            }
            case DataType::FLOAT: {
                std::size_t pos = 0;
                const float parsed = std::stof(token, &pos);
                if (pos != token.size()) {
                    outError = "Invalid FLOAT token '" + token + "'.";
                    return false;
                }
                outValue = parsed;
                return true;
            }
            case DataType::STRING: {
                if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
                    outError = "Invalid STRING token '" + token + "'. Expected quoted string.";
                    return false;
                }
                std::string parsed;
                parsed.reserve(token.size() - 2);
                bool escaped = false;
                for (std::size_t i = 1; i + 1 < token.size(); ++i) {
                    const char ch = token[i];
                    if (escaped) {
                        parsed.push_back(ch);
                        escaped = false;
                        continue;
                    }
                    if (ch == '\\') {
                        escaped = true;
                        continue;
                    }
                    parsed.push_back(ch);
                }
                if (escaped) {
                    outError = "Invalid escaped STRING token '" + token + "'.";
                    return false;
                }
                outValue = parsed;
                return true;
            }
        }
    } catch (const std::exception&) {
        outError = "Unable to parse token '" + token + "'.";
        return false;
    }
    outError = "Unknown value parse failure.";
    return false;
}

}  // namespace

bool Database::createTable(const std::string& tableName,
                           const std::vector<Column>& columns,
                           std::string& errorMessage) {
    if (hasTable(tableName)) {
        errorMessage = "Table '" + tableName + "' already exists.";
        return false;
    }

    tables.emplace(tableName, Table(tableName, columns));
    errorMessage.clear();
    return true;
}

bool Database::dropTable(const std::string& tableName, std::string& errorMessage) {
    auto it = tables.find(tableName);
    if (it == tables.end()) {
        errorMessage = "Table '" + tableName + "' does not exist.";
        return false;
    }

    tables.erase(it);
    errorMessage.clear();
    return true;
}

bool Database::hasTable(const std::string& tableName) const {
    return tables.find(tableName) != tables.end();
}

Table* Database::getTable(const std::string& tableName, std::string& errorMessage) {
    auto it = tables.find(tableName);
    if (it == tables.end()) {
        errorMessage = "Table '" + tableName + "' does not exist.";
        return nullptr;
    }

    errorMessage.clear();
    return &it->second;
}

const Table* Database::getTable(const std::string& tableName, std::string& errorMessage) const {
    auto it = tables.find(tableName);
    if (it == tables.end()) {
        errorMessage = "Table '" + tableName + "' does not exist.";
        return nullptr;
    }

    errorMessage.clear();
    return &it->second;
}

std::vector<std::string> Database::listTables() const {
    std::vector<std::string> tableNames;
    tableNames.reserve(tables.size());
    for (const auto& entry : tables) {
        tableNames.push_back(entry.first);
    }

    std::sort(tableNames.begin(), tableNames.end());
    return tableNames;
}

bool Database::describeTable(const std::string& tableName,
                             std::vector<Column>& outColumns,
                             std::string& errorMessage) const {
    auto it = tables.find(tableName);
    if (it == tables.end()) {
        errorMessage = "Table '" + tableName + "' does not exist.";
        outColumns.clear();
        return false;
    }

    outColumns = it->second.getColumns();
    errorMessage.clear();
    return true;
}

bool Database::saveToDisk(const std::string& dataDirectory, std::string& errorMessage) const {
    namespace fs = std::filesystem;
    try {
        fs::create_directories(dataDirectory);
    } catch (const std::exception& ex) {
        errorMessage = "Failed to create data directory: " + std::string(ex.what());
        return false;
    }

    std::vector<std::string> currentTables = listTables();
    std::sort(currentTables.begin(), currentTables.end());
    auto hasTableName = [&](const std::string& name) {
        return std::binary_search(currentTables.begin(), currentTables.end(), name);
    };

    for (const auto& entry : fs::directory_iterator(dataDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext != ".schema" && ext != ".data") {
            continue;
        }
        const std::string tableName = entry.path().stem().string();
        if (hasTableName(tableName)) {
            continue;
        }
        std::error_code ec;
        fs::remove(entry.path(), ec);
        if (ec) {
            errorMessage = "Failed to remove stale data file '" + entry.path().string() + "'.";
            return false;
        }
    }

    for (const auto& tableName : listTables()) {
        auto it = tables.find(tableName);
        if (it == tables.end()) {
            continue;
        }
        const Table& table = it->second;

        const fs::path schemaPath = fs::path(dataDirectory) / (tableName + ".schema");
        const fs::path dataPath = fs::path(dataDirectory) / (tableName + ".data");

        std::ofstream schemaOut(schemaPath, std::ios::trunc);
        if (!schemaOut) {
            errorMessage = "Failed to open schema file for table '" + tableName + "'.";
            return false;
        }
        for (const auto& column : table.getColumns()) {
            schemaOut << column.getName() << ' ' << dataTypeToString(column.getType()) << '\n';
        }

        std::ofstream dataOut(dataPath, std::ios::trunc);
        if (!dataOut) {
            errorMessage = "Failed to open data file for table '" + tableName + "'.";
            return false;
        }
        for (const auto& row : table.getRows()) {
            const auto& values = row.getValues();
            for (std::size_t i = 0; i < values.size(); ++i) {
                dataOut << serializeValue(values[i]);
                if (i + 1 < values.size()) {
                    dataOut << ',';
                }
            }
            dataOut << '\n';
        }
    }

    errorMessage.clear();
    return true;
}

bool Database::loadFromDisk(const std::string& dataDirectory, std::string& errorMessage) {
    namespace fs = std::filesystem;
    tables.clear();

    if (!fs::exists(dataDirectory)) {
        errorMessage.clear();
        return true;
    }

    for (const auto& entry : fs::directory_iterator(dataDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".schema") {
            continue;
        }

        const std::string tableName = entry.path().stem().string();
        std::ifstream schemaIn(entry.path());
        if (!schemaIn) {
            errorMessage = "Failed to open schema file '" + entry.path().string() + "'.";
            tables.clear();
            return false;
        }

        std::vector<Column> columns;
        std::string line;
        while (std::getline(schemaIn, line)) {
            if (line.empty()) {
                continue;
            }
            std::istringstream lineStream(line);
            std::string columnName;
            std::string typeToken;
            if (!(lineStream >> columnName >> typeToken)) {
                errorMessage = "Malformed schema line in table '" + tableName + "': " + line;
                tables.clear();
                return false;
            }
            DataType parsedType = DataType::STRING;
            if (!parseDataType(typeToken, parsedType)) {
                errorMessage = "Unknown data type '" + typeToken + "' in table '" + tableName + "'.";
                tables.clear();
                return false;
            }
            columns.emplace_back(columnName, parsedType);
        }

        std::string opError;
        if (!createTable(tableName, columns, opError)) {
            errorMessage = opError;
            tables.clear();
            return false;
        }

        const fs::path dataPath = fs::path(dataDirectory) / (tableName + ".data");
        if (!fs::exists(dataPath)) {
            continue;
        }

        std::ifstream dataIn(dataPath);
        if (!dataIn) {
            errorMessage = "Failed to open data file '" + dataPath.string() + "'.";
            tables.clear();
            return false;
        }

        Table* table = getTable(tableName, opError);
        if (table == nullptr) {
            errorMessage = opError;
            tables.clear();
            return false;
        }

        while (std::getline(dataIn, line)) {
            if (line.empty()) {
                continue;
            }

            std::vector<std::string> tokens;
            std::string splitError;
            if (!splitCsvLine(line, tokens, splitError)) {
                errorMessage = "Malformed data row in table '" + tableName + "': " + splitError;
                tables.clear();
                return false;
            }

            if (tokens.size() != columns.size()) {
                errorMessage = "Column/value count mismatch while loading table '" + tableName + "'.";
                tables.clear();
                return false;
            }

            Row row;
            for (std::size_t i = 0; i < columns.size(); ++i) {
                Value parsedValue;
                if (!parseValueForType(tokens[i], columns[i].getType(), parsedValue, splitError)) {
                    errorMessage = "Failed to parse value in table '" + tableName + "': " + splitError;
                    tables.clear();
                    return false;
                }
                row.addValue(parsedValue);
            }

            if (!table->insertRow(row, opError)) {
                errorMessage = "Failed to load row in table '" + tableName + "': " + opError;
                tables.clear();
                return false;
            }
        }
    }

    errorMessage.clear();
    return true;
}
