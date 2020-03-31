#include "parser.hpp"
#include <stdexcept>

void skipws(std::string_view& source, Location& loc) {
	while(!source.empty() && (std::isspace(source[0]) || source[0] == ';')) {
		if(source[0] == ';') { // comment
			auto n = source.find('\n');
			if(n == source.npos) {
				// empty the string
				auto rem = source.length();
				source = source.substr(rem);
				loc.col += rem;
				return;
			}

			++loc.row;
			loc.col = 0;
			source = source.substr(n + 1);
			continue;
		} else if(source[0] == '\n') {
			++loc.row;
			loc.col = 0;
		} else {
			++loc.col;
		}
		source = source.substr(1);
	}
}

void skipws(Parser& parser) {
	skipws(parser.source, parser.loc);
}

std::string_view consume(std::string_view& source, unsigned n, Location& loc) {
	auto token = source.substr(0, n);
	source = source.substr(n);
	loc.col += n;
	return token;
}

void throwError(std::string msg, const Location& loc) {
	msg = std::to_string(loc.row) + ":" + std::to_string(loc.col) + ": " + msg;
	throw std::runtime_error(msg);
}

Expression nextExpression(std::string_view& view, Location& loc) {
	if(view.empty()) {
		throwError("Empty expression (unexpected source end)", loc);
	}

	skipws(view, loc);
	auto oloc = loc;

	// 1: test whether it's a number
	const char* end = &*view.end();
	double value = std::strtod(view.data(), const_cast<char**>(&end));
	if(end != view.data()) {
		auto count = end - view.data();
		view = view.substr(count);
		loc.col += count;
		return {value, oloc};
	}

	// 2: test whether it's a string
	if(view[0] == '"') {
		consume(view, 1, loc); // skip initial "
		auto end = view.find('"');
		if(end == view.npos) {
			throwError("Unterminated '\"'", oloc);
		}


		auto str = consume(view, end, loc);
		consume(view, 1, loc); // skip final "
		return {str, oloc};
	}

	// 3: test if it's application
	if(view[0] == '(') {
		consume(view, 1, loc);
		++loc.depth;

		skipws(view, loc);
		List list;
		while(!view.empty() && view[0] != ')') {
			auto e = nextExpression(view, loc);
			list.values.push_back(e);
			skipws(view, loc);
		}

		--loc.depth;
		if(view.empty()) {
			throwError("Unterminted '('", oloc);
		}

		if(view[0] != ')') {
			throwError("Invalid termination of expression", loc);
		}

		consume(view, 1, loc);
		return {list, loc};
	}

	// otherwise it's an identifier
	auto term = view.find_first_of("\n\t\r\v\f ()");
	if(term == view.npos) {
		throwError("Invalid expression", oloc);
	}

	auto name = consume(view, term, loc);
	if(name == "true") {
		return {true, oloc};
	} else if(name == "false") {
		return {false, oloc};
	}

	return {Identifier{name}, oloc};
}

Expression nextExpression(Parser& p) {
	return nextExpression(p.source, p.loc);
}
