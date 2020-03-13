#include "spirv.hpp"
#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>

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

using u32 = std::uint32_t;

unsigned pushString(std::vector<u32>& buf, const char* str) {
	unsigned i = 0u;
	u32 current = 0u;
	auto count = 0u;
	while(*str != '\0') {
		current |= u32(*str) << (i * 8);
		++str;

		if(++i == 4) {
			++count;
			buf.push_back(current);
			current = 0u;
			i = 0u;
		}
	}

	// we always push it back, making sure we include the null terminator
	buf.push_back(current);
	++count;
	return count;
}

template<typename T>
std::enable_if_t<std::is_enum_v<T>, u32> write(std::vector<u32>& buf, T val) {
	buf.push_back(val);
	return 1;
}

u32 write(std::vector<u32>& buf, u32 val) {
	buf.push_back(val);
	return 1;
}

// u32 write(std::vector<u32>& buf, float val) {
// 	u32 v;
// 	std::memcpy(&v, &val, 4);
// 	buf.push_back(v);
// 	return 1;
// }

u32 write(std::vector<u32>& buf, const char* val) {
	return pushString(buf, val);
}

template<typename... Args>
u32 write(std::vector<u32>& buf, spv::Op opcode, Args&&... args) {
	auto start = buf.size();
	buf.push_back(0); // patched below
	auto wordCount = (1 + ... + write(buf, args));
	buf[start] = (wordCount << 16) | opcode;
	return wordCount;
}

enum class PrimitiveType {
	eVoid,
	eFloat,
};

struct VectorType {
	unsigned count;
	PrimitiveType primitive;
};

struct MatrixType {
	unsigned rows, cols;
	PrimitiveType primitive;
};

using Type = std::variant<PrimitiveType, VectorType, MatrixType>;

struct Context {
	std::vector<u32> buf;
	u32 id {};

	struct {
		u32 tf32 {};
		u32 tvec4 {};
		u32 tvoid {};
	} types;

	struct Output {
		u32 id;
		u32 location;
		u32 idtype;
	};

	struct Constant {
		u32 id;
		u32 value;
		u32 type;
	};

	std::vector<Output> outputs;
	std::vector<Constant> constants;
};

struct GenExpr {
	u32 id;
	u32 idtype;
	Type type;
};

GenExpr generate(Context& ctx, const Expression& expr) {
	if(expr.index() == 0) {
		float val = std::get<0>(expr);
		u32 v;
		std::memcpy(&v, &val, 4);

		// all constants have to be declared at the start of the program
		// and we therefore backpatch them to the start later on
		auto oid = ++ctx.id;
		ctx.constants.push_back({oid, v, ctx.types.tf32});
		return {oid, ctx.types.tf32, PrimitiveType::eFloat};
	}

	if(expr.index() == 1) {
		throw std::runtime_error("Can't generate string");
	}

	auto& app = std::get<2>(expr);
	if(app.name == "+") {
		if(app.arguments.size() != 2) {
			throw std::runtime_error("+ expects 2 arguments");
		}

		auto e1 = generate(ctx, app.arguments[0]);
		auto e2 = generate(ctx, app.arguments[1]);
		// TODO: check if type is addable
		// if(e1.type != e2.type) {
		// 	throw std::runtime_error("Addition arguments must have same type");
		// }

		auto oid = ++ctx.id;
		write(ctx.buf, spv::OpFAdd, e1.idtype, oid, e1.id, e2.id);
		return {oid, e1.idtype, e1.type};
	} else if(app.name == "vec4") {
		if(app.arguments.size() != 4) {
			throw std::runtime_error("vec4 expects 4 arguments");
		}

		auto e1 = generate(ctx, app.arguments[0]);
		auto e2 = generate(ctx, app.arguments[1]);
		auto e3 = generate(ctx, app.arguments[2]);
		auto e4 = generate(ctx, app.arguments[3]);

		// TODO: check that all types are floats

		auto oid = ++ctx.id;
		write(ctx.buf, spv::OpCompositeConstruct, ctx.types.tvec4,
			oid, e1.id, e2.id, e3.id, e4.id);

		auto type = VectorType{4, PrimitiveType::eFloat};
		return {oid, ctx.types.tvec4, type};
	} else if(app.name == "output") {
		if(app.arguments.size() != 2) {
			throw std::runtime_error("output expects 2 arguments");
		}

		auto& a1 = app.arguments[0];
		if(a1.index() != 0) {
			throw std::runtime_error("First argument of output must be int");
		}

		auto e1 = generate(ctx, app.arguments[1]);

		unsigned output = std::get<0>(a1);
		auto oid = ++ctx.id;
		ctx.outputs.push_back({oid, output, e1.idtype});

		write(ctx.buf, spv::OpStore, oid, e1.id);

		return {0, 0, PrimitiveType::eVoid};
	}

	throw std::runtime_error("Unknown function");
}

std::vector<u32> compile(const Expression& expr) {
	constexpr u32 versionNum = 0x00010300; // 1.3

	Context ctx;
	std::vector<u32> header;
	std::vector<u32> sec8;
	std::vector<u32> sec9;

	// - header -
	header.push_back(spv::MagicNumber);
	header.push_back(versionNum);
	header.push_back(0); // generators magic number
	auto boundidx = header.size();
	header.push_back(0); // bound, set later on
	header.push_back(0); // reserved

	auto idglslext = ++ctx.id;
	auto idmain = ++ctx.id;

	write(header, spv::OpCapability, spv::CapabilityShader);
	write(header, spv::OpExtInstImport, idglslext, "GLSL.std.450");
	write(header, spv::OpMemoryModel,
		spv::AddressingModelLogical,
		spv::MemoryModelGLSL450);
	write(header, spv::OpEntryPoint, spv::ExecutionModelFragment, idmain, "main");
	write(header, spv::OpExecutionMode, idmain, spv::ExecutionModeOriginUpperLeft);

	// section 9
	// types
	ctx.types.tf32 = ++ctx.id;
	ctx.types.tvoid = ++ctx.id;
	ctx.types.tvec4 = ++ctx.id;

	write(sec9, spv::OpTypeFloat, ctx.types.tf32, 32);
	write(sec9, spv::OpTypeVoid, ctx.types.tvoid);
	write(sec9, spv::OpTypeVector, ctx.types.tvec4, ctx.types.tf32, 4);

	auto idmaintype = ++ctx.id;
	write(sec9, spv::OpTypeFunction, idmaintype, ctx.types.tvoid);

	// - body -
	// entry point function
	write(ctx.buf, spv::OpFunction, ctx.types.tvoid, idmain,
		spv::FunctionControlMaskNone, idmaintype);

	auto ret = generate(ctx, expr);
	if(ret.type.index() != 0 || std::get<0>(ret.type) != PrimitiveType::eVoid) {
		throw std::runtime_error("Expression wasn't toplevel");
	}

	write(ctx.buf, spv::OpReturn);
	write(ctx.buf, spv::OpFunctionEnd);

	// back-patch the missed global stuff
	for(auto& constant : ctx.constants) {
		write(sec9, spv::OpConstant,
			constant.type,
			constant.id,
			constant.value);
	}

	for(auto& output : ctx.outputs) {
		auto oid = ++ctx.id;
		// TODO: declare types only once
		write(sec9, spv::OpTypePointer, oid,
			spv::StorageClassOutput, output.idtype);
		write(sec9, spv::OpVariable, oid, output.id,
			spv::StorageClassOutput);

		write(sec8, spv::OpDecorate, output.id, spv::DecorationLocation,
			output.location);
	}

	header[boundidx] = ctx.id;

	// join buffers
	header.insert(header.end(), sec8.begin(), sec8.end());
	header.insert(header.end(), sec9.begin(), sec9.end());
	header.insert(header.end(), ctx.buf.begin(), ctx.buf.end());

	return header;
}

void writeFile(std::string_view filename, const std::vector<u32>& buffer) {
	auto openmode = std::ios::binary;
	std::ofstream ofs(std::string{filename}, openmode);
	ofs.exceptions(std::ostream::failbit | std::ostream::badbit);
	auto data = reinterpret_cast<const char*>(buffer.data());
	ofs.write(data, buffer.size() * 4);
}

int main() {
	std::string source = "(output 0 (vec4 (+ 1.0 -0.2) 1.0 0.4 1.0))";
	std::string_view sourcev = source;
	auto expr = parseExpression(sourcev);
	assert(sourcev.empty());
	std::cout << dump(expr) << "\n";

	auto buf = compile(expr);
	writeFile("test.spv", buf);
}
