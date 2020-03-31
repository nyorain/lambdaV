#pragma once

#include <string>
#include <variant>
#include <vector>

struct Expression;

struct Location {
	unsigned row {0};
	unsigned col {0};
	unsigned depth {0};
};

struct List {
	std::vector<Expression> values;
};

struct Identifier {
	std::string_view name;
};

struct Expression {
	std::variant<bool, double, std::string_view, List, Identifier> value;
	Location loc;
};

struct Parser {
	std::string_view source;
	Location loc {};
};

void skipws(Parser& p);
Expression nextExpression(Parser& p);
