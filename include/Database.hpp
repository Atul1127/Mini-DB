#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "Table.hpp"

class Database {
public:
    bool createTable(const std::string& tableName, const std::vector<Column>& columns, std::string& errorMessage);
    bool dropTable(const std::string& tableName, std::string& errorMessage);
    bool hasTable(const std::string& tableName) const;
    Table* getTable(const std::string& tableName, std::string& errorMessage);
    const Table* getTable(const std::string& tableName, std::string& errorMessage) const;
    std::vector<std::string> listTables() const;
    bool describeTable(const std::string& tableName, std::vector<Column>& outColumns, std::string& errorMessage) const;
    bool saveToDisk(const std::string& dataDirectory, std::string& errorMessage) const;
    bool loadFromDisk(const std::string& dataDirectory, std::string& errorMessage);

private:
    std::unordered_map<std::string, Table> tables;
};

#endif
