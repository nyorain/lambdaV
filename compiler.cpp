#include "fwd.hpp"
#include "parser.hpp"

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

CExpression wrap(const Expression& expr) {
	return std::visit([&](auto& val) {
		return CExpression{val, expr.loc};
	}, expr.value);
}

std::string dump(const Expression& expr) {
	// oh wow, this almost looks like proper programming
	// guess C++ has pattern matching after all, eh?
	return std::visit(Visitor{
		[](bool val) { return std::to_string(val); },
		[](double val) { return std::to_string(val); },
		[](std::string_view val) { return std::string(val); },
		[](const Identifier& id) { return std::string(id.name); },
		[](const List& list) {
			std::string ret = "(";
			auto first = true;
			for(auto& arg : list.values) {
				if(!first) {
					ret += " ";
				}

				first = false;
				ret += dump(arg);
			}

			ret += ")";
			return ret;
		}
	}, expr.value);
}

void writeFile(std::string_view filename, const std::vector<u32>& buffer) {
	auto openmode = std::ios::binary;
	std::ofstream ofs(std::string{filename}, openmode);
	ofs.exceptions(std::ostream::failbit | std::ostream::badbit);
	auto data = reinterpret_cast<const char*>(buffer.data());
	ofs.write(data, buffer.size() * 4);
}

void printHelp() {
	std::cout << "Usage: lambdav <source>\n";
	std::cout << "\tWill produce output.spv\n";
}

std::string readFile(std::string_view filename) {
	auto openmode = std::ios::ate;
	std::ifstream ifs(std::string{filename}, openmode);
	ifs.exceptions(std::ostream::failbit | std::ostream::badbit);

	auto size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);

	std::string buffer;
	buffer.resize(size);
	ifs.read(buffer.data(), size);
	return buffer;
}

int main(int argc, const char** argv) {
	if(argc < 2 || !std::strcmp(argv[1], "-h") ||
			!std::strcmp(argv[1], "--help")) {
		printHelp();
		return -1;
	}

	auto input = argv[1];
	std::string source;
	try {
		source = readFile(input);
	} catch(const std::exception& err) {
		std::cout << "Can't read input: " << err.what() << "\n";
		return -2;
	}

	Parser parser {source};

	Codegen codegen;
	Defs defs;
	Context ctx {codegen, defs};
	init(codegen);
	skipws(parser);

	while(!parser.source.empty()) {
		auto expr = nextExpression(parser);

		if(auto list = std::get_if<List>(&expr.value);
				list && !list->values.empty() &&
				std::holds_alternative<Identifier>(list->values[0].value) &&
				std::get<Identifier>(list->values[0].value).name == "define") {

			if(list->values.size() != 3) {
				throwError("Define needs 2 arguments", expr.loc);
			}

			// TODO: check name for keywords/builtins?
			auto name = std::get<Identifier>(list->values[1].value).name;
			std::cout << "define: " << name << " " << dump(list->values[2]) << "\n";
			defs.insert_or_assign(name, DefExpr{wrap(list->values[2]), &defs});
		} else {
			auto ret = generateExpr(ctx, expr);

			// make sure it's an instruction, i.e. top-level expression,
			// i.e. has type void
			if(auto pt = std::get_if<PrimitiveType>(&ret.type);
					!pt || *pt != PrimitiveType::eVoid) {
				throwError("Expression wasn't toplevel", expr.loc);
			}
		}

		skipws(parser);
	}

	auto buf = finish(codegen);
	writeFile("test.spv", buf);
}
