#include "ApiServer.hpp"

#if defined(MINI_DB_HAS_CROW)
#include "crow.h"
#endif

#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string escapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char ch : input) {
        switch (ch) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output.push_back(ch);
                break;
        }
    }
    return output;
}

std::string valueToJson(const Value& value) {
    if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    }
    if (std::holds_alternative<float>(value)) {
        std::ostringstream out;
        out << std::get<float>(value);
        return out.str();
    }
    return "\"" + escapeJson(std::get<std::string>(value)) + "\"";
}

std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::string makeRouteMessageResponse(bool success,
                                     const std::string& route,
                                     const std::string& message,
                                     const std::vector<std::pair<std::string, std::string>>& extraStringFields = {}) {
    std::string response = "{\"success\":";
    response += success ? "true" : "false";
    response += ",\"route\":\"" + escapeJson(route) + "\",\"message\":\"" + escapeJson(message) + "\"";
    for (const auto& field : extraStringFields) {
        response += ",\"" + escapeJson(field.first) + "\":\"" + escapeJson(field.second) + "\"";
    }
    response += "}";
    return response;
}

constexpr std::size_t kDefaultLogTailLines = 50;
constexpr std::size_t kDefaultPerfTailLines = 50;

std::string removeWhitespaceOutsideStrings(const std::string& input) {
    std::string output;
    bool inString = false;
    bool escaped = false;

    for (char ch : input) {
        if (escaped) {
            output.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            output.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            output.push_back(ch);
            inString = !inString;
            continue;
        }
        if (!inString && std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        output.push_back(ch);
    }

    return output;
}

bool validateBasicJsonObject(const std::string& json, std::string& outError) {
    if (json.empty()) {
        outError = "Request body cannot be empty.";
        return false;
    }
    if (json.front() != '{' || json.back() != '}') {
        outError = "Request body must be a JSON object.";
        return false;
    }

    int objectDepth = 0;
    int arrayDepth = 0;
    bool inString = false;
    bool escaped = false;

    for (char ch : json) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }

        if (ch == '{') {
            ++objectDepth;
        } else if (ch == '}') {
            --objectDepth;
            if (objectDepth < 0) {
                outError = "Malformed JSON object.";
                return false;
            }
        } else if (ch == '[') {
            ++arrayDepth;
        } else if (ch == ']') {
            --arrayDepth;
            if (arrayDepth < 0) {
                outError = "Malformed JSON array.";
                return false;
            }
        }
    }

    if (escaped || inString) {
        outError = "Unterminated string literal in request body.";
        return false;
    }
    if (objectDepth != 0) {
        outError = "Unbalanced JSON object braces in request body.";
        return false;
    }
    if (arrayDepth != 0) {
        outError = "Unbalanced JSON array brackets in request body.";
        return false;
    }

    return true;
}

bool normalizeAndValidateJsonObjectRequest(const std::string& requestBody,
                                           std::string& outJson,
                                           std::string& outError) {
    outJson = removeWhitespaceOutsideStrings(trim(requestBody));
    return validateBasicJsonObject(outJson, outError);
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

bool extractStringField(const std::string& json,
                        const std::string& fieldName,
                        std::string& outValue,
                        std::string& outError) {
    const std::string marker = "\"" + fieldName + "\":\"";
    const auto start = json.find(marker);
    if (start == std::string::npos) {
        outError = "Missing string field '" + fieldName + "'.";
        return false;
    }

    const std::size_t valueStart = start + marker.size();
    std::string parsed;
    bool escaped = false;
    for (std::size_t i = valueStart; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            parsed.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            outValue = parsed;
            return true;
        }
        parsed.push_back(ch);
    }

    outError = "Unterminated string field '" + fieldName + "'.";
    return false;
}

bool parseCreateTableRequest(const std::string& requestBody,
                             std::string& outTableName,
                             std::vector<Column>& outColumns,
                             std::string& outError) {
    outTableName.clear();
    outColumns.clear();

    std::string json;
    if (!normalizeAndValidateJsonObjectRequest(requestBody, json, outError)) {
        return false;
    }

    if (!extractStringField(json, "name", outTableName, outError)) {
        return false;
    }
    if (outTableName.empty()) {
        outError = "Table name cannot be empty.";
        return false;
    }

    const std::string columnsMarker = "\"columns\":[";
    const auto columnsStart = json.find(columnsMarker);
    if (columnsStart == std::string::npos) {
        outError = "Missing array field 'columns'.";
        return false;
    }

    const std::size_t arrayStart = columnsStart + columnsMarker.size();
    const auto arrayEnd = json.find(']', arrayStart);
    if (arrayEnd == std::string::npos) {
        outError = "Unterminated 'columns' array.";
        return false;
    }

    const std::string columnsBody = json.substr(arrayStart, arrayEnd - arrayStart);
    if (columnsBody.empty()) {
        outError = "Columns array cannot be empty.";
        return false;
    }

    std::size_t cursor = 0;
    while (cursor < columnsBody.size()) {
        const auto objectStart = columnsBody.find('{', cursor);
        if (objectStart == std::string::npos) {
            break;
        }
        const auto objectEnd = columnsBody.find('}', objectStart);
        if (objectEnd == std::string::npos) {
            outError = "Malformed column object.";
            return false;
        }

        const std::string objectJson = columnsBody.substr(objectStart, objectEnd - objectStart + 1);
        std::string columnName;
        std::string typeToken;
        if (!extractStringField(objectJson, "name", columnName, outError)) {
            return false;
        }
        if (!extractStringField(objectJson, "type", typeToken, outError)) {
            return false;
        }

        if (columnName.empty()) {
            outError = "Column name cannot be empty.";
            return false;
        }

        DataType parsedType = DataType::STRING;
        if (!parseDataType(typeToken, parsedType)) {
            outError = "Unknown data type '" + typeToken + "' for column '" + columnName + "'.";
            return false;
        }

        outColumns.emplace_back(columnName, parsedType);
        cursor = objectEnd + 1;
        if (cursor < columnsBody.size() && columnsBody[cursor] == ',') {
            ++cursor;
        }
    }

    if (outColumns.empty()) {
        outError = "Columns array cannot be empty.";
        return false;
    }

    return true;
}

std::vector<std::string> splitJsonArrayValues(const std::string& input, std::string& outError) {
    std::vector<std::string> tokens;
    std::string current;
    bool inString = false;
    bool escaped = false;

    for (char ch : input) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            current.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            current.push_back(ch);
            inString = !inString;
            continue;
        }
        if (ch == ',' && !inString) {
            if (current.empty()) {
                outError = "Malformed values array near comma.";
                return {};
            }
            tokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (inString) {
        outError = "Unterminated string literal in values array.";
        return {};
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool parseJsonValueToken(const std::string& token, Value& outValue, std::string& outError) {
    if (token.empty()) {
        outError = "Empty value token.";
        return false;
    }

    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
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
            outError = "Malformed escaped string token.";
            return false;
        }
        outValue = parsed;
        return true;
    }

    try {
        std::size_t pos = 0;
        const int intValue = std::stoi(token, &pos);
        if (pos == token.size()) {
            outValue = intValue;
            return true;
        }
    } catch (const std::exception&) {
    }

    try {
        std::size_t pos = 0;
        const float floatValue = std::stof(token, &pos);
        if (pos == token.size()) {
            outValue = floatValue;
            return true;
        }
    } catch (const std::exception&) {
    }

    outError = "Unsupported value token '" + token + "'.";
    return false;
}

bool parseInsertRowRequest(const std::string& requestBody,
                           std::vector<Value>& outValues,
                           std::string& outError) {
    outValues.clear();

    std::string json;
    if (!normalizeAndValidateJsonObjectRequest(requestBody, json, outError)) {
        return false;
    }

    const std::string marker = "\"values\":[";
    const auto start = json.find(marker);
    if (start == std::string::npos) {
        outError = "Missing array field 'values'.";
        return false;
    }

    const std::size_t arrayStart = start + marker.size();
    const auto arrayEnd = json.find(']', arrayStart);
    if (arrayEnd == std::string::npos) {
        outError = "Unterminated 'values' array.";
        return false;
    }

    const std::string valuesBody = json.substr(arrayStart, arrayEnd - arrayStart);
    if (valuesBody.empty()) {
        outError = "Values array cannot be empty.";
        return false;
    }

    std::string splitError;
    const std::vector<std::string> rawTokens = splitJsonArrayValues(valuesBody, splitError);
    if (!splitError.empty()) {
        outError = splitError;
        return false;
    }

    for (const auto& rawToken : rawTokens) {
        Value parsedValue;
        if (!parseJsonValueToken(rawToken, parsedValue, outError)) {
            return false;
        }
        outValues.push_back(parsedValue);
    }

    if (outValues.empty()) {
        outError = "Values array cannot be empty.";
        return false;
    }

    return true;
}

bool extractObjectField(const std::string& json,
                        const std::string& fieldName,
                        std::string& outObject,
                        std::string& outError) {
    const std::string marker = "\"" + fieldName + "\":{";
    const auto start = json.find(marker);
    if (start == std::string::npos) {
        outError = "Missing object field '" + fieldName + "'.";
        return false;
    }

    const std::size_t objectStart = start + marker.size() - 1;
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;

    for (std::size_t i = objectStart; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                outObject = json.substr(objectStart, i - objectStart + 1);
                return true;
            }
        }
    }

    outError = "Unterminated object field '" + fieldName + "'.";
    return false;
}

bool parseValueFieldFromObject(const std::string& objectJson, Value& outValue, std::string& outError) {
    const std::string marker = "\"value\":";
    const auto start = objectJson.find(marker);
    if (start == std::string::npos) {
        outError = "Missing field 'value'.";
        return false;
    }

    const std::size_t valueStart = start + marker.size();
    if (valueStart >= objectJson.size()) {
        outError = "Empty 'value' field.";
        return false;
    }

    std::string token;
    if (objectJson[valueStart] == '"') {
        token.push_back('"');
        bool escaped = false;
        for (std::size_t i = valueStart + 1; i < objectJson.size(); ++i) {
            const char ch = objectJson[i];
            token.push_back(ch);
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                break;
            }
        }
    } else {
        for (std::size_t i = valueStart; i < objectJson.size(); ++i) {
            const char ch = objectJson[i];
            if (ch == ',' || ch == '}') {
                break;
            }
            token.push_back(ch);
        }
    }

    token = trim(token);
    if (token.empty()) {
        outError = "Empty 'value' field.";
        return false;
    }

    return parseJsonValueToken(token, outValue, outError);
}

bool parseUpdateRowsRequest(const std::string& requestBody,
                            std::string& outSetColumn,
                            Value& outSetValue,
                            std::string& outWhereColumn,
                            Value& outWhereValue,
                            std::string& outError) {
    outSetColumn.clear();
    outWhereColumn.clear();

    std::string json;
    if (!normalizeAndValidateJsonObjectRequest(requestBody, json, outError)) {
        return false;
    }

    std::string setObject;
    if (!extractObjectField(json, "set", setObject, outError)) {
        return false;
    }
    if (!extractStringField(setObject, "column", outSetColumn, outError)) {
        return false;
    }
    if (outSetColumn.empty()) {
        outError = "SET column cannot be empty.";
        return false;
    }
    if (!parseValueFieldFromObject(setObject, outSetValue, outError)) {
        return false;
    }

    std::string whereObject;
    if (!extractObjectField(json, "where", whereObject, outError)) {
        return false;
    }
    if (!extractStringField(whereObject, "column", outWhereColumn, outError)) {
        return false;
    }
    if (outWhereColumn.empty()) {
        outError = "WHERE column cannot be empty.";
        return false;
    }
    if (!parseValueFieldFromObject(whereObject, outWhereValue, outError)) {
        return false;
    }

    return true;
}

bool parseDeleteRowsRequest(const std::string& requestBody,
                            std::string& outWhereColumn,
                            Value& outWhereValue,
                            std::string& outError) {
    outWhereColumn.clear();

    std::string json;
    if (!normalizeAndValidateJsonObjectRequest(requestBody, json, outError)) {
        return false;
    }

    std::string whereObject;
    if (!extractObjectField(json, "where", whereObject, outError)) {
        return false;
    }
    if (!extractStringField(whereObject, "column", outWhereColumn, outError)) {
        return false;
    }
    if (outWhereColumn.empty()) {
        outError = "WHERE column cannot be empty.";
        return false;
    }
    if (!parseValueFieldFromObject(whereObject, outWhereValue, outError)) {
        return false;
    }

    return true;
}

bool parseSchemaMutationRequest(const std::string& requestBody,
                                std::string& outAction,
                                std::string& outColumnName,
                                DataType& outColumnType,
                                bool& outHasType,
                                std::string& outError) {
    outAction.clear();
    outColumnName.clear();
    outHasType = false;

    std::string json;
    if (!normalizeAndValidateJsonObjectRequest(requestBody, json, outError)) {
        return false;
    }

    if (!extractStringField(json, "action", outAction, outError)) {
        return false;
    }
    if (outAction.empty()) {
        outError = "Action cannot be empty.";
        return false;
    }

    std::string columnObject;
    if (!extractObjectField(json, "column", columnObject, outError)) {
        return false;
    }
    if (!extractStringField(columnObject, "name", outColumnName, outError)) {
        return false;
    }
    if (outColumnName.empty()) {
        outError = "Column name cannot be empty.";
        return false;
    }

    std::string typeToken;
    if (extractStringField(columnObject, "type", typeToken, outError)) {
        outHasType = true;
        if (!parseDataType(typeToken, outColumnType)) {
            outError = "Unknown data type '" + typeToken + "'.";
            return false;
        }
    } else if (outAction == "ADD_COLUMN") {
        outError = "Missing string field 'type'.";
        return false;
    } else {
        outError.clear();
        outHasType = false;
    }

    return true;
}

}  // namespace

ApiServer::ApiServer(Database& database, std::string dataDirectory)
    : database(database),
      dataDirectory(std::move(dataDirectory)),
      routes{
          "GET /tables",
          "GET /tables/:name",
          "GET /tables/:name/rows",
          "POST /tables",
          "PUT /tables/:name/schema",
          "POST /tables/:name/rows",
          "PUT /tables/:name/rows",
          "DELETE /tables/:name/rows",
          "DELETE /tables/:name",
          "GET /logs",
          "GET /performance",
      } {}

void ApiServer::start(unsigned short port) {
#if defined(MINI_DB_HAS_CROW)
    crow::SimpleApp app;

    auto jsonResponse = [](std::string body) {
        crow::response response;
        response.code = 200;
        response.set_header("Content-Type", "application/json");
        response.body = std::move(body);
        return response;
    };

    using namespace crow;

    CROW_ROUTE(app, "/tables").methods("GET"_method)([this, jsonResponse]() {
        return jsonResponse(handleTables());
    });

    CROW_ROUTE(app, "/tables").methods("POST"_method)([this, jsonResponse](const crow::request& request) {
        return jsonResponse(handleCreateTable(request.body));
    });

    CROW_ROUTE(app, "/tables/<string>").methods("GET"_method)(
        [this, jsonResponse](const std::string& tableName) {
            return jsonResponse(handleTableSummary(tableName));
        });

    CROW_ROUTE(app, "/tables/<string>").methods("DELETE"_method)(
        [this, jsonResponse](const std::string& tableName) {
            return jsonResponse(handleDeleteTable(tableName));
        });

    CROW_ROUTE(app, "/tables/<string>/rows").methods("GET"_method)(
        [this, jsonResponse](const std::string& tableName) {
            return jsonResponse(handleTableRows(tableName));
        });

    CROW_ROUTE(app, "/tables/<string>/rows").methods("POST"_method)(
        [this, jsonResponse](const crow::request& request, const std::string& tableName) {
            return jsonResponse(handleInsertRow(tableName, request.body));
        });

    CROW_ROUTE(app, "/tables/<string>/rows").methods("PUT"_method)(
        [this, jsonResponse](const crow::request& request, const std::string& tableName) {
            return jsonResponse(handleUpdateRows(tableName, request.body));
        });

    CROW_ROUTE(app, "/tables/<string>/rows").methods("DELETE"_method)(
        [this, jsonResponse](const crow::request& request, const std::string& tableName) {
            return jsonResponse(handleDeleteRows(tableName, request.body));
        });

    CROW_ROUTE(app, "/tables/<string>/schema").methods("PUT"_method)(
        [this, jsonResponse](const crow::request& request, const std::string& tableName) {
            return jsonResponse(handleUpdateSchema(tableName, request.body));
        });

    CROW_ROUTE(app, "/logs").methods("GET"_method)([this, jsonResponse]() {
        return jsonResponse(handleLogs());
    });

    CROW_ROUTE(app, "/performance").methods("GET"_method)([this, jsonResponse]() {
        return jsonResponse(handlePerformance());
    });

    std::cout << "Mini DB API server listening on http://127.0.0.1:" << port << '\n';
    app.port(port).multithreaded().run();
#else
    (void)port;
    std::cout << "Crow is not available. Reconfigure with MINI_DB_FETCH_CROW=ON or vendor third_party/crow/crow.h." << '\n';
#endif
}

std::vector<std::string> ApiServer::registeredRoutes() const {
    return routes;
}

std::string ApiServer::handleTables() const {
    const auto tableNames = database.listTables();

    std::ostringstream response;
    response << R"({"success":true,"route":"/tables","tables":[)";
    for (std::size_t i = 0; i < tableNames.size(); ++i) {
        response << '"' << escapeJson(tableNames[i]) << '"';
        if (i + 1 < tableNames.size()) {
            response << ',';
        }
    }
    response << "]}";
    return response.str();
}

std::string ApiServer::handleTableSummary(const std::string& tableName) const {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name", "Table name cannot be empty.");
    }

    std::string errorMessage;
    const Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name", errorMessage);
    }

    std::ostringstream response;
    response << "{\"success\":true,\"route\":\"/tables/:name\",\"table\":{";
    response << "\"name\":\"" << escapeJson(table->getName()) << "\",";
    response << "\"rowCount\":" << table->getRows().size() << ',';
    response << "\"columns\":[";

    const auto& columns = table->getColumns();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        response << "{\"name\":\"" << escapeJson(columns[i].getName()) << "\",";
        response << "\"type\":\"" << escapeJson(columns[i].getTypeAsString()) << "\"}";
        if (i + 1 < columns.size()) {
            response << ',';
        }
    }

    response << "]}}";
    return response.str();
}

std::string ApiServer::handleTableRows(const std::string& tableName) const {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", "Table name cannot be empty.");
    }

    std::string errorMessage;
    const Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    std::ostringstream response;
    response << "{\"success\":true,\"route\":\"/tables/:name/rows\",";
    response << "\"table\":\"" << escapeJson(normalizedName) << "\",";
    response << "\"rows\":[";

    const auto& rows = table->getRows();
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        response << '[';
        const auto& values = rows[rowIndex].getValues();
        for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            response << valueToJson(values[valueIndex]);
            if (valueIndex + 1 < values.size()) {
                response << ',';
            }
        }
        response << ']';
        if (rowIndex + 1 < rows.size()) {
            response << ',';
        }
    }
    response << "]}";
    return response.str();
}

std::string ApiServer::handleCreateTable(const std::string& requestBody) {
    std::string tableName;
    std::vector<Column> columns;
    std::string errorMessage;

    if (!parseCreateTableRequest(requestBody, tableName, columns, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables", errorMessage);
    }

    if (!database.createTable(tableName, columns, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables", errorMessage);
    }

    return makeRouteMessageResponse(true, "/tables", "Table '" + tableName + "' created successfully.");
}

std::string ApiServer::handleUpdateSchema(const std::string& tableName, const std::string& requestBody) {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name/schema", "Table name cannot be empty.");
    }

    std::string action;
    std::string columnName;
    DataType columnType = DataType::STRING;
    bool hasType = false;
    std::string errorMessage;
    if (!parseSchemaMutationRequest(requestBody, action, columnName, columnType, hasType, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/schema", errorMessage);
    }

    Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name/schema", errorMessage);
    }

    bool ok = false;
    if (action == "ADD_COLUMN") {
        if (!hasType) {
            return makeRouteMessageResponse(false, "/tables/:name/schema", "ADD_COLUMN requires a column type.");
        }
        ok = table->addColumn(Column(columnName, columnType), errorMessage);
    } else if (action == "DROP_COLUMN") {
        ok = table->dropColumn(columnName, errorMessage);
    } else {
        return makeRouteMessageResponse(false, "/tables/:name/schema", "Unsupported action '" + action + "'.");
    }

    if (!ok) {
        return makeRouteMessageResponse(false, "/tables/:name/schema", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/schema", errorMessage);
    }

    if (action == "ADD_COLUMN") {
        return makeRouteMessageResponse(
            true,
            "/tables/:name/schema",
            "Column '" + columnName + "' added to table '" + normalizedName + "'.");
    }
    return makeRouteMessageResponse(
        true,
        "/tables/:name/schema",
        "Column '" + columnName + "' dropped from table '" + normalizedName + "'.");
}

std::string ApiServer::handleInsertRow(const std::string& tableName, const std::string& requestBody) {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", "Table name cannot be empty.");
    }

    std::vector<Value> values;
    std::string errorMessage;
    if (!parseInsertRowRequest(requestBody, values, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    Row row;
    for (const auto& value : values) {
        row.addValue(value);
    }

    if (!table->insertRow(row, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    return makeRouteMessageResponse(true, "/tables/:name/rows", "Row inserted into table '" + normalizedName + "'.");
}

std::string ApiServer::handleUpdateRows(const std::string& tableName, const std::string& requestBody) {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", "Table name cannot be empty.");
    }

    std::string setColumn;
    Value setValue;
    std::string whereColumn;
    Value whereValue;
    std::string errorMessage;

    if (!parseUpdateRowsRequest(requestBody, setColumn, setValue, whereColumn, whereValue, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    std::size_t updatedCount = 0;
    if (!table->updateWhereEquals(setColumn, setValue, whereColumn, whereValue, updatedCount, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    return makeRouteMessageResponse(
        true,
        "/tables/:name/rows",
        "Updated " + std::to_string(updatedCount) + " row(s) in table '" + normalizedName + "'.");
}

std::string ApiServer::handleDeleteRows(const std::string& tableName, const std::string& requestBody) {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", "Table name cannot be empty.");
    }

    std::string whereColumn;
    Value whereValue;
    std::string errorMessage;
    if (!parseDeleteRowsRequest(requestBody, whereColumn, whereValue, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    Table* table = database.getTable(normalizedName, errorMessage);
    if (table == nullptr) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    std::size_t deletedCount = 0;
    if (!table->deleteWhereEquals(whereColumn, whereValue, deletedCount, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name/rows", errorMessage);
    }

    return makeRouteMessageResponse(
        true,
        "/tables/:name/rows",
        "Deleted " + std::to_string(deletedCount) + " row(s) from table '" + normalizedName + "'.");
}

std::string ApiServer::handleDeleteTable(const std::string& tableName) {
    const std::string normalizedName = trim(tableName);
    if (normalizedName.empty()) {
        return makeRouteMessageResponse(false, "/tables/:name", "Table name cannot be empty.");
    }

    std::string errorMessage;
    if (!database.dropTable(normalizedName, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name", errorMessage);
    }

    if (!database.saveToDisk(dataDirectory, errorMessage)) {
        return makeRouteMessageResponse(false, "/tables/:name", errorMessage);
    }

    return makeRouteMessageResponse(true, "/tables/:name", "Table '" + normalizedName + "' deleted successfully.");
}

std::string ApiServer::handleLogs() const {
    const std::filesystem::path logPath = std::filesystem::path("logs") / "mini-db.log";
    std::ifstream input(logPath);
    if (!input) {
        std::error_code ec;
        if (!std::filesystem::exists(logPath, ec)) {
            return "{\"success\":true,\"route\":\"/logs\",\"message\":\"Log file not found.\",\"log_file\":\"" +
                   escapeJson(logPath.string()) + "\",\"count\":0,\"logs\":[]}";
        }
        return "{\"success\":false,\"route\":\"/logs\",\"message\":\"Failed to read log file.\",\"log_file\":\"" +
               escapeJson(logPath.string()) + "\",\"count\":0,\"logs\":[]}";
    }

    std::deque<std::string> recentLines;
    std::string line;
    while (std::getline(input, line)) {
        recentLines.push_back(line);
        if (recentLines.size() > kDefaultLogTailLines) {
            recentLines.pop_front();
        }
    }

    std::string logsJson;
    for (std::size_t i = 0; i < recentLines.size(); ++i) {
        if (i != 0) {
            logsJson += ',';
        }
        logsJson += "\"" + escapeJson(recentLines[i]) + "\"";
    }

    if (recentLines.empty()) {
        return "{\"success\":true,\"route\":\"/logs\",\"message\":\"Log file is empty.\",\"log_file\":\"" +
               escapeJson(logPath.string()) + "\",\"count\":0,\"logs\":[]}";
    }

    return "{\"success\":true,\"route\":\"/logs\",\"message\":\"Recent logs fetched.\",\"log_file\":\"" +
           escapeJson(logPath.string()) + "\",\"count\":" + std::to_string(recentLines.size()) +
           ",\"logs\":[" + logsJson + "]}";
}

std::string ApiServer::handlePerformance() const {
    const std::filesystem::path logPath = std::filesystem::path("logs") / "mini-db.log";
    std::ifstream input(logPath);
    if (!input) {
        std::error_code ec;
        if (!std::filesystem::exists(logPath, ec)) {
            return "{\"success\":true,\"route\":\"/performance\",\"message\":\"Log file not found.\",\"log_file\":\"" +
                   escapeJson(logPath.string()) +
                   "\",\"count\":0,\"parsed_count\":0,\"min_us\":0,\"max_us\":0,\"avg_us\":0,\"entries\":[]}";
        }
        return "{\"success\":false,\"route\":\"/performance\",\"message\":\"Failed to read log file.\",\"log_file\":\"" +
               escapeJson(logPath.string()) +
               "\",\"count\":0,\"parsed_count\":0,\"min_us\":0,\"max_us\":0,\"avg_us\":0,\"entries\":[]}";
    }

    std::deque<std::string> recentPerfLines;
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("[PERF]") == std::string::npos) {
            continue;
        }
        recentPerfLines.push_back(line);
        if (recentPerfLines.size() > kDefaultPerfTailLines) {
            recentPerfLines.pop_front();
        }
    }

    if (recentPerfLines.empty()) {
        return "{\"success\":true,\"route\":\"/performance\",\"message\":\"No performance entries found.\",\"log_file\":\"" +
               escapeJson(logPath.string()) +
               "\",\"count\":0,\"parsed_count\":0,\"min_us\":0,\"max_us\":0,\"avg_us\":0,\"entries\":[]}";
    }

    long long minUs = std::numeric_limits<long long>::max();
    long long maxUs = std::numeric_limits<long long>::lowest();
    long long sumUs = 0;
    std::size_t parsedCount = 0;

    std::string entriesJson;
    for (std::size_t i = 0; i < recentPerfLines.size(); ++i) {
        const std::string& perfLine = recentPerfLines[i];

        long long parsedUs = 0;
        bool hasParsedUs = false;
        const auto usPos = perfLine.rfind(" us");
        if (usPos != std::string::npos) {
            const auto digitEnd = usPos;
            const auto digitStart = perfLine.find_last_not_of("0123456789", digitEnd - 1);
            if (digitStart != std::string::npos && digitStart + 1 < digitEnd) {
                const std::string digits = perfLine.substr(digitStart + 1, digitEnd - (digitStart + 1));
                try {
                    parsedUs = std::stoll(digits);
                    hasParsedUs = true;
                } catch (const std::exception&) {
                    hasParsedUs = false;
                }
            }
        }

        if (hasParsedUs) {
            minUs = std::min(minUs, parsedUs);
            maxUs = std::max(maxUs, parsedUs);
            sumUs += parsedUs;
            ++parsedCount;
        }

        if (i != 0) {
            entriesJson += ',';
        }
        entriesJson += "{\"line\":\"" + escapeJson(perfLine) + "\",\"execution_us\":";
        if (hasParsedUs) {
            entriesJson += std::to_string(parsedUs);
        } else {
            entriesJson += "null";
        }
        entriesJson += "}";
    }

    if (parsedCount == 0) {
        minUs = 0;
        maxUs = 0;
    }
    const long long avgUs = parsedCount == 0 ? 0 : (sumUs / static_cast<long long>(parsedCount));

    return "{\"success\":true,\"route\":\"/performance\",\"message\":\"Performance summary generated.\",\"log_file\":\"" +
           escapeJson(logPath.string()) + "\",\"count\":" + std::to_string(recentPerfLines.size()) +
           ",\"parsed_count\":" + std::to_string(parsedCount) + ",\"min_us\":" + std::to_string(minUs) +
           ",\"max_us\":" + std::to_string(maxUs) + ",\"avg_us\":" + std::to_string(avgUs) +
           ",\"entries\":[" + entriesJson + "]}";
}
