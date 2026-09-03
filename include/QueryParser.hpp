#ifndef QUERY_PARSER_HPP
#define QUERY_PARSER_HPP

#include <string>
#include <vector>

#include "Database.hpp"
#include "Row.hpp"

struct QueryResult {
    bool success = false;
    std::string message;
    std::vector<Row> rows;
};

class QueryParser {
public:
    explicit QueryParser(Database& database);
    QueryResult execute(const std::string& query);

private:
    Database& database;
};

#endif
