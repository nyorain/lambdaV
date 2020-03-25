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

void skipws(std::string_view& source) {
	while(std::isspace(source[0])) {
		source = source.substr(1);
	}
}

Expression parseExpression(std::string_view& view) {
	if(view.empty()) {
		throw std::runtime_error("Invalid empty expression");
	}

	skipws(view);

	// 1: test whether it's a number
	const char* end = &*view.end();
	double value = std::strtod(view.data(), const_cast<char**>(&end));
	if(end != view.data()) {
		view = view.substr(end - view.data());
		return {value};
	}

	// 2: test whether it's a string
	if(view[0] == '"') {
		auto end = view.find('"', 1);
		if(end == view.npos) {
			throw std::runtime_error("Unterminated '\"'");
		}

		return {view.substr(0, end)};
	}

	// 3: test if it's application
	if(view[0] == '(') {
		view = view.substr(1);
		auto term = view.find_first_of("\n\t\r\v\f (");
		if(term == view.npos) {
			throw std::runtime_error("Invalid application");
		}

		auto name = view.substr(0, term);
		view = view.substr(term);

		// TODO: check if it is a defined function
		// auto it = std::find_if(defs.begin(), defs.end(), [&](auto& def) {
		// 	return def.name == name;
		// });
		// if(it != defs.end()) {
		// 	auto ait = std::get_if<Application>(&it->expression);
		// 	// TODO: handle first-class function values
		// 	if(!ait || ait->name != "func") {
		// 		throw std::runtime_error("Can't call defined value like that");
		// 	}
		// }

		// otherwise: builtin i guess?
		List app;
		app.values = {name};

		while(!view.empty() && std::isspace(view[0])) {
			skipws(view);
			auto e = parseExpression(view);
			app.values.push_back(e);
		}

		skipws(view);
		if(view[0] != ')') {
			throw std::runtime_error("Invalid termination of expression");
		}

		view = view.substr(1);
		return {app};
	}

	// otherwise it's an identifier
	auto term = view.find_first_of("\n\t\r\v\f )");
	if(term == view.npos) {
		throw std::runtime_error("Invalid expression");
	}

	auto name = view.substr(0, term);
	view = view.substr(term);
	return {Identifier{name}};

	// TODO: move where it's needed
	// auto it = std::find_if(defs.begin(), defs.end(), [&](auto& def) {
	// 	return def.name == name;
	// });
	// if(it == defs.end()) {
	// 	std::string msg = "Undefined identifier: '";
	// 	msg += name;
	// 	msg += "'";
	// 	msg += " left: ";
	// 	msg += view;
	// 	throw std::runtime_error(msg);
	// }
//
	// auto def = &*it;
	// return {def};
}

std::string dump(const Expression& expr) {
	switch(expr.index()) {
		case 0: return std::to_string(std::get<0>(expr));
		case 1: return std::string(std::get<1>(expr));
		case 3: return std::string(std::get<3>(expr).name);
		default: break;
	}

	auto& app = std::get<2>(expr);
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
	(define plus2 (func (x) (+ x 2)))
	(define white (vec4 (plus2 -1) 1.0 0.4 1.0))
	(output 0 white)
)SRC";

int main() {
	std::string_view sourcev = source;
	std::vector<Definition> defs;

	Codegen codegen;
	init(codegen);

	while(!sourcev.empty()) {
		auto expr = parseExpression(sourcev);

		if(auto list = std::get_if<List>(&expr);
				list->values.size() == 2 && list->values[0].index() == 3) {
			auto name = std::get<3>(list->values[0]).name;
			defs.push_back({name, list->values[1]});
		} else {
			// make sure it's an instruction, i.e. top-level expression,
			// i.e. has type void
			auto ret = generate(codegen, expr, defs);
			if(ret.type.index() != 0 ||
					std::get<0>(ret.type) != PrimitiveType::eVoid) {
				throw std::runtime_error("Expression wasn't toplevel");
			}
		}

		skipws(sourcev);
	}

	auto buf = finish(codegen);
	writeFile("test.spv", buf);
}
