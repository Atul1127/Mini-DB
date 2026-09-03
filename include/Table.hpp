#ifndef TABLE_HPP
#define TABLE_HPP

#include <string>
#include <vector>
#include <cstddef>

#include "Column.hpp"
#include "Row.hpp"

class Table {
public:
    Table(std::string name, std::vector<Column> columns);

    const std::string& getName() const;
    const std::vector<Column>& getColumns() const;
    const std::vector<Row>& getRows() const;
    bool insertRow(const Row& row, std::string& errorMessage);
    bool deleteWhereEquals(const std::string& columnName,
                           const Value& value,
                           std::size_t& deletedCount,
                           std::string& errorMessage);
    bool updateWhereEquals(const std::string& targetColumnName,
                           const Value& targetValue,
                           const std::string& matchColumnName,
                           const Value& matchValue,
                           std::size_t& updatedCount,
                           std::string& errorMessage);
    bool addColumn(const Column& column, std::string& errorMessage);
    bool dropColumn(const std::string& columnName, std::string& errorMessage);

private:
    std::string name;
    std::vector<Column> columns;
    std::vector<Row> rows;
};

#endif
