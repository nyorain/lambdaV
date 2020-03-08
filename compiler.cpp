#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <cassert>
#include <iostream>

struct Application;
using Expression = std::variant<double, std::string_view, Application>;

struct Application {
	std::string_view name;
	std::vector<Expression> arguments;
};

Expression parseExpression(std::string_view& view) {
	if(view.empty()) {
		throw std::runtime_error("Invalid empty expression");
	}

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
		auto term = view.find_first_of(" )");
		if(term == view.npos) {
			throw std::runtime_error("Invalid application");
		}

		Application app;
		app.name = view.substr(0, term);

		view = view.substr(term);
		while(!view.empty() && view[0] == ' ') {
			view = view.substr(1);
			app.arguments.push_back(parseExpression(view));
		}

		if(view[0] != ')') {
			throw std::runtime_error("Invalid termination of expression");
		}

		view = view.substr(1);
		return {app};
	}

	throw std::runtime_error("Invalid expression");
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

int main() {
	std::string source = "(output 0 (vec4 1.0 1.0 1.0 1.0))";
	std::string_view sourcev = source;
	auto expr = parseExpression(sourcev);
	assert(sourcev.empty());
	std::cout << dump(expr) << "\n";
}
