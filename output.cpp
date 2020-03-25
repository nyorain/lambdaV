#include "spirv.hpp"
#include "fwd.hpp"

#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>

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

GenExpr generate(Codegen& ctx, const Expression& expr) {
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

	// TODO
	if(expr.index() == 1) {
		throw std::runtime_error("Can't generate string");
	}

	if(expr.index() == 3) {
		auto& def = std::get<3>(expr);
		return generate(ctx, def->expression);
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

void init(Codegen& ctx) {
	// reserve ids
	ctx.idmain = ++ctx.id;
	ctx.idmaintype = ++ctx.id;
	ctx.idglsl = ++ctx.id;

	ctx.types.tf32 = ++ctx.id;
	ctx.types.tvoid = ++ctx.id;
	ctx.types.tvec4 = ++ctx.id;

	// entry point function
	write(ctx.buf, spv::OpFunction, ctx.types.tvoid, ctx.idmain,
		spv::FunctionControlMaskNone, ctx.idmaintype);
	write(ctx.buf, spv::OpLabel, ++ctx.id);
}

std::vector<u32> finish(Codegen& ctx) {
	// finish ctx buf (main function)
	write(ctx.buf, spv::OpReturn);
	write(ctx.buf, spv::OpFunctionEnd);

	// write header
	constexpr u32 versionNum = 0x00010300; // 1.3
	std::vector<u32> buf;

	buf.push_back(spv::MagicNumber);
	buf.push_back(versionNum);
	buf.push_back(0); // generators magic number

	auto maxboundid = buf.size();
	buf.push_back(0); // max bound, filled in the end
	buf.push_back(0); // reserved

	write(buf, spv::OpCapability, spv::CapabilityShader);
	write(buf, spv::OpExtInstImport, ctx.idglsl, "GLSL.std.450");
	write(buf, spv::OpMemoryModel,
		spv::AddressingModelLogical,
		spv::MemoryModelGLSL450);
	write(buf, spv::OpEntryPoint, spv::ExecutionModelFragment, ctx.idmain, "main");
	write(buf, spv::OpExecutionMode, ctx.idmain, spv::ExecutionModeOriginUpperLeft);

	// section 9
	// types
	write(buf, spv::OpTypeFloat, ctx.types.tf32, 32);
	write(buf, spv::OpTypeVoid, ctx.types.tvoid);
	write(buf, spv::OpTypeVector, ctx.types.tvec4, ctx.types.tf32, 4);
	write(buf, spv::OpTypeFunction, ctx.idmaintype, ctx.types.tvoid);

	// back-patch the missed global stuff
	for(auto& constant : ctx.constants) {
		write(buf, spv::OpConstant,
			constant.type,
			constant.id,
			constant.value);
	}

	for(auto& output : ctx.outputs) {
		auto oid = ++ctx.id;
		// TODO: declare types only once
		write(buf, spv::OpTypePointer, oid,
			spv::StorageClassOutput, output.idtype);
		write(buf, spv::OpVariable, oid, output.id,
			spv::StorageClassOutput);

		write(buf, spv::OpDecorate, output.id, spv::DecorationLocation,
			output.location);
	}

	buf[maxboundid] = ctx.id;
	buf.insert(buf.end(), ctx.buf.begin(), ctx.buf.end());
	return buf;
}
