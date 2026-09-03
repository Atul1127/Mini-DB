#include "QueryParser.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>

namespace {

constexpr const char* kDataDirectory = "data";

std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::vector<std::string> tokenizeBySpace(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream stream(input);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string toUpper(const std::string& input) {
    std::string output = input;
    std::transform(output.begin(), output.end(), output.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return output;
}

bool startsWithIgnoreCase(const std::string& input, const std::string& prefix) {
    if (input.size() < prefix.size()) {
        return false;
    }

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(input[i])) !=
            std::toupper(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool parseDataType(const std::string& rawType, DataType& outType) {
    const std::string upperType = toUpper(trim(rawType));
    if (upperType == "INT") {
        outType = DataType::INT;
        return true;
    }
    if (upperType == "FLOAT") {
        outType = DataType::FLOAT;
        return true;
    }
    if (upperType == "STRING") {
        outType = DataType::STRING;
        return true;
    }
    return false;
}

bool parseCreateTableCommand(const std::string& queryBody,
                             std::string& outTableName,
                             std::vector<Column>& outColumns,
                             std::string& outError) {
    outTableName.clear();
    outColumns.clear();

    const std::string rest = trim(queryBody);
    if (rest.empty()) {
        outError = "Expected syntax CREATE TABLE <name> or CREATE TABLE <name> (col TYPE, ...).";
        return false;
    }

    const auto openParenPos = rest.find('(');
    if (openParenPos == std::string::npos) {
        if (rest.find(' ') != std::string::npos || rest.find('\t') != std::string::npos) {
            outError = "Expected syntax CREATE TABLE <name>.";
            return false;
        }
        outTableName = rest;
        return true;
    }

    outTableName = trim(rest.substr(0, openParenPos));
    if (outTableName.empty()) {
        outError = "Missing table name in CREATE TABLE.";
        return false;
    }

    const std::string schemaWithParen = trim(rest.substr(openParenPos));
    if (schemaWithParen.size() < 2 || schemaWithParen.front() != '(' || schemaWithParen.back() != ')') {
        outError = "Malformed schema definition. Expected (...).";
        return false;
    }

    const std::string schemaBody = trim(schemaWithParen.substr(1, schemaWithParen.size() - 2));
    if (schemaBody.empty()) {
        outError = "Schema cannot be empty.";
        return false;
    }

    std::size_t start = 0;
    while (start < schemaBody.size()) {
        const auto commaPos = schemaBody.find(',', start);
        const std::string rawDefinition = trim(
            schemaBody.substr(start, commaPos == std::string::npos ? std::string::npos : commaPos - start));

        if (rawDefinition.empty()) {
            outError = "Malformed schema definition near comma.";
            return false;
        }

        const auto parts = tokenizeBySpace(rawDefinition);
        if (parts.size() != 2) {
            outError = "Invalid column definition '" + rawDefinition + "'. Expected <name> <TYPE>.";
            return false;
        }

        DataType parsedType = DataType::STRING;
        if (!parseDataType(parts[1], parsedType)) {
            outError = "Unknown data type '" + parts[1] + "' for column '" + parts[0] + "'.";
            return false;
        }
        outColumns.emplace_back(parts[0], parsedType);

        if (commaPos == std::string::npos) {
            break;
        }
        start = commaPos + 1;
        if (trim(schemaBody.substr(start)).empty()) {
            outError = "Malformed schema definition near trailing comma.";
            return false;
        }
    }

    return true;
}

std::vector<std::string> splitCommaSeparated(const std::string& input, std::string& outError) {
    std::vector<std::string> parts;
    std::string current;
    bool inQuotes = false;

    for (char ch : input) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            current.push_back(ch);
            continue;
        }

        if (ch == ',' && !inQuotes) {
            const std::string piece = trim(current);
            if (piece.empty()) {
                outError = "Malformed values list near comma.";
                return {};
            }
            parts.push_back(piece);
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (inQuotes) {
        outError = "Unterminated string literal in values list.";
        return {};
    }

    const std::string finalPiece = trim(current);
    if (finalPiece.empty() && !input.empty()) {
        outError = "Malformed values list near trailing comma.";
        return {};
    }
    if (!finalPiece.empty()) {
        parts.push_back(finalPiece);
    }

    return parts;
}

bool parseValueLiteral(const std::string& token, Value& outValue, std::string& outError) {
    const std::string value = trim(token);
    if (value.empty()) {
        outError = "Empty value literal.";
        return false;
    }

    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        outValue = value.substr(1, value.size() - 2);
        return true;
    }

    try {
        std::size_t intPos = 0;
        const int intValue = std::stoi(value, &intPos);
        if (intPos == value.size()) {
            outValue = intValue;
            return true;
        }
    } catch (const std::exception&) {
        // Fall through to float parsing.
    }

    try {
        std::size_t floatPos = 0;
        const float floatValue = std::stof(value, &floatPos);
        if (floatPos == value.size()) {
            outValue = floatValue;
            return true;
        }
    } catch (const std::exception&) {
        // Handled below.
    }

    outError = "Unable to parse value literal '" + value + "'.";
    return false;
}

bool parseInsertCommand(const std::string& query,
                        std::string& outTableName,
                        std::vector<Value>& outValues,
                        std::string& outError) {
    outTableName.clear();
    outValues.clear();

    if (!startsWithIgnoreCase(query, "INSERT INTO ")) {
        outError = "Expected syntax INSERT INTO <table> VALUES (...).";
        return false;
    }

    const std::string body = query.substr(std::string("INSERT INTO ").size());
    const std::string upperBody = toUpper(body);
    const std::size_t valuesPos = upperBody.find(" VALUES ");
    if (valuesPos == std::string::npos) {
        outError = "Expected VALUES clause in INSERT command.";
        return false;
    }

    outTableName = trim(body.substr(0, valuesPos));
    if (outTableName.empty()) {
        outError = "Missing table name in INSERT command.";
        return false;
    }

    const std::string valuesExpr = trim(body.substr(valuesPos + std::string(" VALUES ").size()));
    if (valuesExpr.size() < 2 || valuesExpr.front() != '(' || valuesExpr.back() != ')') {
        outError = "Malformed VALUES list. Expected (...).";
        return false;
    }

    const std::string valuesBody = trim(valuesExpr.substr(1, valuesExpr.size() - 2));
    if (valuesBody.empty()) {
        return true;
    }

    std::string splitError;
    const std::vector<std::string> rawValues = splitCommaSeparated(valuesBody, splitError);
    if (!splitError.empty()) {
        outError = splitError;
        return false;
    }

    for (const auto& rawValue : rawValues) {
        Value parsedValue;
        if (!parseValueLiteral(rawValue, parsedValue, outError)) {
            return false;
        }
        outValues.push_back(parsedValue);
    }

    return true;
}

std::size_t findCharOutsideQuotes(const std::string& input, char target) {
    bool inQuotes = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && input[i] == target) {
            return i;
        }
    }
    return std::string::npos;
}

bool parseEqualityCondition(const std::string& condition,
                            std::string& outColumnName,
                            Value& outValue,
                            std::string& outError) {
    outColumnName.clear();

    const std::size_t equalsPos = findCharOutsideQuotes(condition, '=');
    if (equalsPos == std::string::npos) {
        outError = "Expected equality condition <column> = <value>.";
        return false;
    }

    outColumnName = trim(condition.substr(0, equalsPos));
    const std::string rawValue = trim(condition.substr(equalsPos + 1));
    if (outColumnName.empty() || rawValue.empty()) {
        outError = "Malformed condition. Expected <column> = <value>.";
        return false;
    }

    if (!parseValueLiteral(rawValue, outValue, outError)) {
        return false;
    }

    return true;
}

bool parseDeleteCommand(const std::string& query,
                        std::string& outTableName,
                        std::string& outColumnName,
                        Value& outValue,
                        std::string& outError) {
    outTableName.clear();
    outColumnName.clear();

    if (!startsWithIgnoreCase(query, "DELETE FROM ")) {
        outError = "Expected syntax DELETE FROM <table> WHERE <column> = <value>.";
        return false;
    }

    const std::string body = query.substr(std::string("DELETE FROM ").size());
    const std::string upperBody = toUpper(body);
    const std::size_t wherePos = upperBody.find(" WHERE ");
    if (wherePos == std::string::npos) {
        outError = "Expected WHERE clause in DELETE command.";
        return false;
    }

    outTableName = trim(body.substr(0, wherePos));
    if (outTableName.empty()) {
        outError = "Missing table name in DELETE command.";
        return false;
    }

    const std::string condition = trim(body.substr(wherePos + std::string(" WHERE ").size()));
    if (condition.empty()) {
        outError = "Missing DELETE condition after WHERE.";
        return false;
    }

    if (!parseEqualityCondition(condition, outColumnName, outValue, outError)) {
        if (outError == "Malformed condition. Expected <column> = <value>.") {
            outError = "Malformed DELETE condition. Expected <column> = <value>.";
        }
        return false;
    }

    return true;
}

bool parseUpdateCommand(const std::string& query,
                        std::string& outTableName,
                        std::string& outTargetColumn,
                        Value& outTargetValue,
                        std::string& outMatchColumn,
                        Value& outMatchValue,
                        std::string& outError) {
    outTableName.clear();
    outTargetColumn.clear();
    outMatchColumn.clear();

    if (!startsWithIgnoreCase(query, "UPDATE ")) {
        outError = "Expected syntax UPDATE <table> SET <column> = <value> WHERE <column> = <value>.";
        return false;
    }

    const std::string body = query.substr(std::string("UPDATE ").size());
    const std::string upperBody = toUpper(body);

    const std::size_t setPos = upperBody.find(" SET ");
    if (setPos == std::string::npos) {
        outError = "Expected SET clause in UPDATE command.";
        return false;
    }

    outTableName = trim(body.substr(0, setPos));
    if (outTableName.empty()) {
        outError = "Missing table name in UPDATE command.";
        return false;
    }

    const std::string setAndWhere = body.substr(setPos + std::string(" SET ").size());
    const std::string upperSetAndWhere = toUpper(setAndWhere);
    const std::size_t wherePos = upperSetAndWhere.find(" WHERE ");
    if (wherePos == std::string::npos) {
        outError = "Expected WHERE clause in UPDATE command.";
        return false;
    }

    const std::string setPart = trim(setAndWhere.substr(0, wherePos));
    const std::string wherePart = trim(setAndWhere.substr(wherePos + std::string(" WHERE ").size()));
    if (setPart.empty() || wherePart.empty()) {
        outError = "Malformed UPDATE command. Expected SET and WHERE conditions.";
        return false;
    }

    if (!parseEqualityCondition(setPart, outTargetColumn, outTargetValue, outError)) {
        if (outError == "Malformed condition. Expected <column> = <value>.") {
            outError = "Malformed SET clause. Expected <column> = <value>.";
        }
        return false;
    }

    if (!parseEqualityCondition(wherePart, outMatchColumn, outMatchValue, outError)) {
        if (outError == "Malformed condition. Expected <column> = <value>.") {
            outError = "Malformed WHERE clause. Expected <column> = <value>.";
        }
        return false;
    }

    return true;
}

bool persistDatabase(Database& database, std::string& outError) {
    return database.saveToDisk(kDataDirectory, outError);
}

}  // namespace

QueryParser::QueryParser(Database& database)
    : database(database) {}

QueryResult QueryParser::execute(const std::string& query) {
    QueryResult result;
    std::string normalized = trim(query);
    if (normalized.empty()) {
        result.success = false;
        result.message = "Error: Empty query.";
        return result;
    }

    if (!normalized.empty() && normalized.back() == ';') {
        normalized.pop_back();
        normalized = trim(normalized);
    }

    const std::vector<std::string> tokens = tokenizeBySpace(normalized);
    if (tokens.empty()) {
        result.success = false;
        result.message = "Error: Empty query.";
        return result;
    }

    const std::string first = toUpper(tokens[0]);
    std::string errorMessage;

    if (first == "CREATE") {
        if (!startsWithIgnoreCase(normalized, "CREATE TABLE ")) {
            result.success = false;
            result.message = "Error: Expected syntax CREATE TABLE <name> or CREATE TABLE <name> (col TYPE, ...).";
            return result;
        }

        std::string tableName;
        std::vector<Column> parsedColumns;
        const std::string createBody = normalized.substr(std::string("CREATE TABLE ").size());
        if (!parseCreateTableCommand(createBody, tableName, parsedColumns, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        const bool ok = database.createTable(tableName, parsedColumns, errorMessage);
        if (ok && !persistDatabase(database, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }
        result.success = ok;
        result.message = ok ? "Table '" + tableName + "' created successfully."
                            : "Error: " + errorMessage;
        return result;
    }

    if (first == "DROP") {
        if (tokens.size() != 3 || toUpper(tokens[1]) != "TABLE") {
            result.success = false;
            result.message = "Error: Expected syntax DROP TABLE <name>.";
            return result;
        }

        const bool ok = database.dropTable(tokens[2], errorMessage);
        if (ok && !persistDatabase(database, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }
        result.success = ok;
        result.message = ok ? "Table '" + tokens[2] + "' dropped successfully."
                            : "Error: " + errorMessage;
        return result;
    }

    if (first == "INSERT") {
        std::string tableName;
        std::vector<Value> parsedValues;
        if (!parseInsertCommand(normalized, tableName, parsedValues, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        Table* table = database.getTable(tableName, errorMessage);
        if (table == nullptr) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        Row row;
        for (const auto& value : parsedValues) {
            row.addValue(value);
        }

        const bool ok = table->insertRow(row, errorMessage);
        if (ok && !persistDatabase(database, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }
        result.success = ok;
        result.message = ok ? "Row inserted into '" + tableName + "'."
                            : "Error: " + errorMessage;
        return result;
    }

    if (first == "SELECT") {
        if (tokens.size() != 4 || tokens[1] != "*" || toUpper(tokens[2]) != "FROM") {
            result.success = false;
            result.message = "Error: Expected syntax SELECT * FROM <table>.";
            return result;
        }

        Table* table = database.getTable(tokens[3], errorMessage);
        if (table == nullptr) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        result.success = true;
        result.rows = table->getRows();
        result.message = "Selected " + std::to_string(result.rows.size()) +
                         " row(s) from '" + tokens[3] + "'.";
        return result;
    }

    if (first == "DELETE") {
        std::string tableName;
        std::string columnName;
        Value parsedValue;
        if (!parseDeleteCommand(normalized, tableName, columnName, parsedValue, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        Table* table = database.getTable(tableName, errorMessage);
        if (table == nullptr) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        std::size_t deletedCount = 0;
        const bool ok = table->deleteWhereEquals(columnName, parsedValue, deletedCount, errorMessage);
        if (ok && !persistDatabase(database, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }
        result.success = ok;
        result.message = ok ? "Deleted " + std::to_string(deletedCount) + " row(s) from '" + tableName + "'."
                            : "Error: " + errorMessage;
        return result;
    }

    if (first == "UPDATE") {
        std::string tableName;
        std::string targetColumn;
        Value targetValue;
        std::string matchColumn;
        Value matchValue;
        if (!parseUpdateCommand(normalized, tableName, targetColumn, targetValue, matchColumn, matchValue, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        Table* table = database.getTable(tableName, errorMessage);
        if (table == nullptr) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        std::size_t updatedCount = 0;
        const bool ok = table->updateWhereEquals(
            targetColumn, targetValue, matchColumn, matchValue, updatedCount, errorMessage);
        if (ok && !persistDatabase(database, errorMessage)) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }
        result.success = ok;
        result.message = ok ? "Updated " + std::to_string(updatedCount) + " row(s) in '" + tableName + "'."
                            : "Error: " + errorMessage;
        return result;
    }

    if (first == "ALTER") {
        if (tokens.size() < 6 || toUpper(tokens[1]) != "TABLE" || toUpper(tokens[4]) != "COLUMN") {
            result.success = false;
            result.message = "Error: Expected syntax ALTER TABLE <name> ADD COLUMN <col> <type> or ALTER TABLE <name> DROP COLUMN <col>.";
            return result;
        }

        const std::string& tableName = tokens[2];
        Table* table = database.getTable(tableName, errorMessage);
        if (table == nullptr) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        const std::string action = toUpper(tokens[3]);
        if (action == "ADD") {
            if (tokens.size() != 7) {
                result.success = false;
                result.message = "Error: Expected syntax ALTER TABLE <name> ADD COLUMN <col> <type>.";
                return result;
            }

            const std::string& columnName = tokens[5];
            const std::string& columnTypeToken = tokens[6];
            DataType parsedType = DataType::STRING;
            if (!parseDataType(columnTypeToken, parsedType)) {
                result.success = false;
                result.message = "Error: Unknown data type '" + columnTypeToken + "'.";
                return result;
            }

            const bool ok = table->addColumn(Column(columnName, parsedType), errorMessage);
            if (ok && !persistDatabase(database, errorMessage)) {
                result.success = false;
                result.message = "Error: " + errorMessage;
                return result;
            }
            result.success = ok;
            result.message = ok ? "Column '" + columnName + "' added to table '" + tableName + "'."
                                : "Error: " + errorMessage;
            return result;
        }

        if (action == "DROP") {
            if (tokens.size() != 6) {
                result.success = false;
                result.message = "Error: Expected syntax ALTER TABLE <name> DROP COLUMN <col>.";
                return result;
            }

            const std::string& columnName = tokens[5];
            const bool ok = table->dropColumn(columnName, errorMessage);
            if (ok && !persistDatabase(database, errorMessage)) {
                result.success = false;
                result.message = "Error: " + errorMessage;
                return result;
            }
            result.success = ok;
            result.message = ok ? "Column '" + columnName + "' dropped from table '" + tableName + "'."
                                : "Error: " + errorMessage;
            return result;
        }

        result.success = false;
        result.message = "Error: ALTER TABLE supports only ADD COLUMN or DROP COLUMN.";
        return result;
    }

    if (first == "SHOW") {
        if (tokens.size() == 2 && toUpper(tokens[1]) == "TABLES") {
            const auto tables = database.listTables();
            result.success = true;
            if (tables.empty()) {
                result.message = "No tables found.";
                return result;
            }

            std::ostringstream message;
            message << "Tables: ";
            for (std::size_t i = 0; i < tables.size(); ++i) {
                message << tables[i];
                if (i + 1 < tables.size()) {
                    message << ", ";
                }
            }
            result.message = message.str();
            return result;
        }

        if (tokens.size() == 3 && toUpper(tokens[1]) == "TABLE") {
            Table* table = database.getTable(tokens[2], errorMessage);
            if (table == nullptr) {
                result.success = false;
                result.message = "Error: " + errorMessage;
                return result;
            }

            result.success = true;
            std::ostringstream message;
            message << "Table " << tokens[2] << ": columns(";
            const auto& columns = table->getColumns();
            for (std::size_t i = 0; i < columns.size(); ++i) {
                message << columns[i].getName() << ' ' << columns[i].getTypeAsString();
                if (i + 1 < columns.size()) {
                    message << ", ";
                }
            }
            message << "), rows=" << table->getRows().size();
            result.message = message.str();
            return result;
        }

        result.success = false;
        result.message = "Error: Expected syntax SHOW TABLES or SHOW TABLE <name>.";
        return result;
    }

    if (first == "DESCRIBE") {
        if (tokens.size() != 2) {
            result.success = false;
            result.message = "Error: Expected syntax DESCRIBE <table>.";
            return result;
        }

        std::vector<Column> columns;
        const bool ok = database.describeTable(tokens[1], columns, errorMessage);
        if (!ok) {
            result.success = false;
            result.message = "Error: " + errorMessage;
            return result;
        }

        result.success = true;
        std::ostringstream message;
        message << "Schema " << tokens[1] << ": ";
        if (columns.empty()) {
            message << "(no columns)";
        } else {
            for (std::size_t i = 0; i < columns.size(); ++i) {
                message << columns[i].getName() << ' ' << columns[i].getTypeAsString();
                if (i + 1 < columns.size()) {
                    message << ", ";
                }
            }
        }
        result.message = message.str();
        return result;
    }

    result.success = false;
    result.message = "Error: Unsupported command in current parser scope.";
    return result;
}
