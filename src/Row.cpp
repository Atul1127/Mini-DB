#include "Row.hpp"

void Row::addValue(const Value& value) {
    values.push_back(value);
}

const std::vector<Value>& Row::getValues() const {
    return values;
}
