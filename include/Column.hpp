#ifndef COLUMN_HPP
#define COLUMN_HPP

#include <string>

enum class DataType {
    INT,
    FLOAT,
    STRING
};

class Column {
public:
    Column(std::string name, DataType type);

    const std::string& getName() const;
    DataType getType() const;
    std::string getTypeAsString() const;

private:
    std::string name;
    DataType type;
};

#endif
