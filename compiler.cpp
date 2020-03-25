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

std::optional<Expression> parseExpression(std::string_view& view,
		std::deque<Definition>& defs) {
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

		// special case: define
		if(name == "define") {
			skipws(view);
			auto term = view.find_first_of("\n\t\r\v\f ");
			if(term == view.npos) {
				throw std::runtime_error("Invalid define");
			}

			auto name = view.substr(0, term);
			view = view.substr(term);
			auto e = parseExpression(view, defs);
			if(!e) {
				throw std::runtime_error("Invalid inline non-expression");
			}

			defs.push_back({name, *e});

			skipws(view);
			if(view[0] != ')') {
				throw std::runtime_error("Invalid termination of expression");
			}
			view = view.substr(1);
			return std::nullopt;
		}

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
		Application app;
		app.name = name;

		while(!view.empty() && std::isspace(view[0])) {
			skipws(view);
			auto e = parseExpression(view, defs);
			if(!e) {
				throw std::runtime_error("Invalid inline non-expression");
			}

			app.arguments.push_back(*e);
		}

		skipws(view);
		if(view[0] != ')') {
			throw std::runtime_error("Invalid termination of expression");
		}

		view = view.substr(1);
		return {app};
	}

	// 4: check if its reference to definition
	auto term = view.find_first_of("\n\t\r\v\f )");
	if(term == view.npos) {
		throw std::runtime_error("Invalid expression");
	}

	auto name = view.substr(0, term);
	view = view.substr(term);

	auto it = std::find_if(defs.begin(), defs.end(), [&](auto& def) {
		return def.name == name;
	});
	if(it == defs.end()) {
		std::string msg = "Undefined identifier: '";
		msg += name;
		msg += "'";
		msg += " left: ";
		msg += view;
		throw std::runtime_error(msg);
	}

	auto def = &*it;
	return {def};
}

std::string dump(const Expression& expr) {
	switch(expr.index()) {
		case 0: return std::to_string(std::get<0>(expr));
		case 1: return std::string(std::get<1>(expr));
		default: break;
	}

	auto& app = std::get<2>(expr);
	std::string ret = "(" + std::string(app.name) + " ";
	auto first = true;
	for(auto& arg : app.arguments) {
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
	(define white (vec4 (+ 1.2 -0.2) 1.0 0.4 1.0))
	(output 0 white)
)SRC";

int main() {
	std::string_view sourcev = source;
	std::deque<Definition> definitions;

	Codegen codegen;
	init(codegen);

	while(!sourcev.empty()) {
		auto expr = parseExpression(sourcev, definitions);
		if(expr) {
			// make sure it's an instruction, i.e. top-level expression,
			// i.e. has type void
			auto ret = generate(codegen, *expr);
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
