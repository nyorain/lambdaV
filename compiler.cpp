#include "fwd.hpp"
#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <deque>

void skipws(std::string_view& source, Location& loc) {
	while(!source.empty() && (std::isspace(source[0]) || source[0] == '#')) {
		if(source[0] == '#') { // comment
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

Expression parseExpression(std::string_view& view, Location& loc) {
	if(view.empty()) {
		throwError("Invalid empty expression", loc);
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

		skipws(view, loc);
		List list;
		while(!view.empty() && view[0] != ')') {
			auto e = parseExpression(view, loc);
			list.values.push_back(e);
			skipws(view, loc);
		}

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

std::string dump(const Expression& expr) {
	switch(expr.value.index()) {
		case 0: return std::to_string(std::get<0>(expr.value));
		case 1: return std::string(std::get<1>(expr.value));
		case 2: break;
		case 3: return std::string(std::get<3>(expr.value).name);
		case 4: return std::to_string(std::get<4>(expr.value));
		default: return "<invalid expr>";
	}

	auto& app = std::get<2>(expr.value);
	std::string ret = "(";

	auto first = true;
	for(auto& arg : app.values) {
		if(!first) {
			ret += " ";
		}

		first = false;
		ret += dump(arg);
	}

	ret += ")";
	return ret;
}

void writeFile(std::string_view filename, const std::vector<u32>& buffer) {
	auto openmode = std::ios::binary;
	std::ofstream ofs(std::string{filename}, openmode);
	ofs.exceptions(std::ostream::failbit | std::ostream::badbit);
	auto data = reinterpret_cast<const char*>(buffer.data());
	ofs.write(data, buffer.size() * 4);
}


const std::string source = R"SRC(
	(define nat-fold (func (n accum func) (
		let ((body (rec-func (n accum) (
				if (eq n 0)
					accum
					(rec (- n 1) (func accum n))
			))))
			(body n accum)
	)))

	# (define plus2 (func (x) (plus x 2)))
	# (define plusc (func (x) (func (y) (+ x y))))
	# (define plus (func (x y) ((plusc x) y)))

	(define sumup-accum (func (accum n) (+ accum n)))
	# sums up all natural numbers up to x
	(define sumup (func (x) (nat-fold x 0 sumup-accum)))

	(define val (sumup 7))
	(define white (vec4 val 1.0 0.4 1.0))
	(output 0 white)
)SRC";

int main() {
	std::string_view sourcev = source;

	Location loc;
	Codegen codegen;
	Defs defs;
	Context ctx {codegen, defs};
	init(codegen);

	while(!sourcev.empty()) {
		auto expr = parseExpression(sourcev, loc);

		if(auto list = std::get_if<List>(&expr.value);
				!list->values.empty() &&
				list->values[0].value.index() == 3 &&
				std::get<Identifier>(list->values[0].value).name == "define") {

			if(list->values.size() != 3) {
				throwError("Define needs 2 arguments", expr.loc);
			}

			// TODO: check name for keywords/builtins?
			auto name = std::get<3>(list->values[1].value).name;
			std::cout << "define: " << name << " " << dump(list->values[2]) << "\n";
			defs.insert_or_assign(name, DefExpr{list->values[2], &defs});
		} else {
			// make sure it's an instruction, i.e. top-level expression,
			// i.e. has type void
			auto ret = generateExpr(ctx, expr);
			if(ret.type.index() != 0 ||
					std::get<0>(ret.type) != PrimitiveType::eVoid) {
				throwError("Expression wasn't toplevel", expr.loc);
			}
		}

		skipws(sourcev, loc);
	}

	auto buf = finish(codegen);
	writeFile("test.spv", buf);
}
