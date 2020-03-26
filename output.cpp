#include "spirv.hpp"
#include "fwd.hpp"
#include <dlg/dlg.hpp>

#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <cassert>
#include <iostream>
#include <algorithm>
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
	buf.push_back(static_cast<u32>(val));
	return 1;
}

u32 write(std::vector<u32>& buf, u32 val) {
	buf.push_back(val);
	return 1;
}

u32 write(std::vector<u32>& buf, const std::vector<u32>& vals) {
	buf.insert(buf.end(), vals.begin(), vals.end());
	return vals.size();
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

// Recursive generation
GenExpr generateCall(Codegen& ctx, const Expression& expr,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs);

using BuiltinGen = GenExpr(*)(Codegen& ctx, const Location& loc,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs);

GenExpr generateIf(Codegen& ctx, const Location& loc,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs) {
	if(args.empty()) {
		throwError("Invalid call nesting", loc);
	}

	if(args.back()->size() != 4) {
		throwError("If needs 3 arguments", loc);
	}

	auto cond = generate(ctx, (*args.back())[1], defs);
	if(cond.idtype != ctx.types.tbool) {
		throwError("if condition must be bool", loc);
	}

	auto nargs = args;
	nargs.pop_back();

	auto tlabel = ++ctx.id;
	auto flabel = ++ctx.id;
	auto dstlabel = ++ctx.id;

	write(ctx.buf, spv::OpSelectionMerge, dstlabel, spv::SelectionControlMaskNone);
	write(ctx.buf, spv::OpBranchConditional, cond.id, tlabel, flabel);

	// true label
	write(ctx.buf, spv::OpLabel, tlabel);
	auto et = generateCall(ctx, (*args.back())[2], nargs, defs);
	write(ctx.buf, spv::OpBranch, dstlabel);

	// false label
	write(ctx.buf, spv::OpLabel, flabel);
	auto ef = generateCall(ctx, (*args.back())[3], nargs, defs);
	write(ctx.buf, spv::OpBranch, dstlabel);

	if(et.idtype != ef.idtype) {
		throwError("if branches have different types", loc);
	}

	// phi
	write(ctx.buf, spv::OpLabel, dstlabel);
	auto phi = ++ctx.id;
	write(ctx.buf, spv::OpPhi, et.idtype, phi,
		et.id, tlabel, ef.id, flabel);
	return {phi, et.idtype, et.type};
}

template<spv::Op Op>
GenExpr generateBinop(Codegen& ctx, const Location& loc,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0]->size() != 3) {
		std::string msg = "binop";
		msg += " expects 2 arguments";
		throwError(msg, loc);
	}

	auto e1 = generate(ctx, (*args[0])[1], defs);
	auto e2 = generate(ctx, (*args[0])[2], defs);
	if(e1.idtype != e2.idtype) {
		std::string msg = "binop";
		msg += " arguments must have same type";
		throwError(msg, loc);
	}

	auto oid = ++ctx.id;
	write(ctx.buf, Op, e1.idtype, oid, e1.id, e2.id);
	return {oid, e1.idtype, e1.type};
}

GenExpr generateVec4(Codegen& ctx, const Location& loc,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0]->size() != 5) {
		throwError("vec4 expects 4 arguments", loc);
	}

	std::vector<u32> ids;
	auto e1 = generate(ctx, (*args[0])[1], defs);
	auto e2 = generate(ctx, (*args[0])[2], defs);
	auto e3 = generate(ctx, (*args[0])[3], defs);
	auto e4 = generate(ctx, (*args[0])[4], defs);

	// TODO: check that all types are floats

	auto oid = ++ctx.id;
	write(ctx.buf, spv::OpCompositeConstruct, ctx.types.tvec4,
		oid, e1.id, e2.id, e3.id, e4.id);

	auto type = VectorType{4, PrimitiveType::eFloat};
	return {oid, ctx.types.tvec4, type};
}

GenExpr generateOutput(Codegen& ctx, const Location& loc,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0]->size() != 3) {
		throwError("output expects 2 arguments", loc);
	}

	auto& a1 = (*args[0])[1];
	if(a1.value.index() != 0) {
		throwError("First argument of output must be int", a1.loc);
	}

	auto e1 = generate(ctx, (*args[0])[2], defs);

	unsigned output = std::get<0>(a1.value);
	auto oid = ++ctx.id;
	ctx.outputs.push_back({oid, output, e1.idtype});

	write(ctx.buf, spv::OpStore, oid, e1.id);

	return {0, 0, PrimitiveType::eVoid};
}

const std::unordered_map<std::string_view, BuiltinGen> builtins = {
	{"if", generateIf},
	{"output", generateOutput},
	{"+", &generateBinop<spv::OpFAdd>},
	{"-", generateBinop<spv::OpFSub>},
	{"*", generateBinop<spv::OpFMul>},
	{"/", generateBinop<spv::OpFDiv>},
	{"vec4", generateVec4},
};

GenExpr generateCall(Codegen& ctx, const Expression& expr,
		const std::vector<const std::vector<Expression>*>& args,
		const std::vector<Definition>& defs) {
	if(args.empty()) {
		return generate(ctx, expr, defs);
	}

	auto& ev = expr.value;
	if(ev.index() == 0 || ev.index() == 1 || ev.index() == 4) {
		throwError("Invalid application; no function", expr.loc);
	}

	// application
	if(ev.index() == 2) {
		auto& list = std::get<List>(ev);

		// special application case: function definition
		if(list.values.size() > 1 &&
				list.values[0].value.index() == 3 &&
				std::get<Identifier>(list.values[0].value).name == "func") {
			auto ndefs = defs;
			auto nargs = args;
			if(list.values.size() != 3) {
				throwError("Invalid function definition (value count)", expr.loc);
			}

			auto argsi = std::get_if<List>(&list.values[1].value);
			if(!argsi) {
				throwError("Invalid function definition (param)", expr.loc);
			}

			if(args.empty()) {
				throwError("Function call without params", expr.loc);
			}

			if(argsi->values.size() + 1 != args.back()->size()) {
				auto msg = dlg::format("Function call with invalid number "
					"of params: Expected {}, got {}", argsi->values.size(),
					args.back()->size());
				throwError(msg, expr.loc);
			}

			for(auto i = 0u; i < argsi->values.size(); ++i) {
				auto name = std::get_if<Identifier>(&argsi->values[i].value);
				if(!name) {
					throwError("Invalid function definition (param identifier)",
						expr.loc);
				}

				ndefs.push_back({name->name, (*args.back())[i + 1]});
			}

			nargs.pop_back();
			auto& body = list.values[2];
			return generateCall(ctx, body, nargs, ndefs);
		}

		auto nargs = args;
		nargs.push_back(&list.values);
		return generateCall(ctx, list.values[0], nargs, defs);
	}

	// identifier: must either be builtin or defined function
	if(ev.index() == 3) {
		auto fname = std::get<Identifier>(ev).name;
		auto it = builtins.find(fname);
		if(it == builtins.end()) {
			auto it = std::find_if(defs.begin(), defs.end(), [&](auto def){
				return def.name == fname;
			});
			if(it == defs.end()) {
				std::string msg = "Unknown function identifier '";
				msg += fname;
				msg += "'";
				throwError(msg, expr.loc);
			}

			return generateCall(ctx, it->expression, args, defs);
		} else {
			return it->second(ctx, expr.loc, args, defs);
		}
	}

	throwError("Invalid expression type", expr.loc);
}


GenExpr generate(Codegen& ctx, const Expression& expr,
		const std::vector<Definition>& defs) {

	// all constants have to be declared at the start of the program
	// and we therefore backpatch them to the start later on
	// constant number
	if(auto* val = std::get_if<double>(&expr.value)) {
		u32 v;
		float f = *val;
		std::memcpy(&v, &f, 4);
		auto oid = ++ctx.id;
		ctx.constants.push_back({oid, v, ctx.types.tf32});
		return {oid, ctx.types.tf32, PrimitiveType::eFloat};
	}

	// constant bool
	if(auto* val = std::get_if<bool>(&expr.value)) {
		u32 id = *val ? ctx.idtrue : ctx.idfalse;
		return {id, ctx.types.tbool, PrimitiveType::eBool};
	}

	// TODO: constant string
	if(expr.value.index() == 1) {
		throwError("Can't generate string", expr.loc);
	}

	// identifier
	if(expr.value.index() == 3) {
		auto& identifier = std::get<3>(expr.value);
		auto it = std::find_if(defs.begin(), defs.end(), [&](auto def){
			return def.name == identifier.name;
		});

		if(it == defs.end()) {
			std::string msg = "Unknown identifier '";
			msg += identifier.name;
			msg += "'";
			throwError(msg, expr.loc);
		}

		return generate(ctx, it->expression, defs);
	}

	// application
	auto& app = std::get<2>(expr.value);
	return generateCall(ctx, app.values[0], {&app.values}, defs);
}

void init(Codegen& ctx) {
	// reserve ids
	ctx.idmain = ++ctx.id;
	ctx.idmaintype = ++ctx.id;
	ctx.idglsl = ++ctx.id;
	ctx.idtrue = ++ctx.id;
	ctx.idfalse = ++ctx.id;

	ctx.types.tf32 = ++ctx.id;
	ctx.types.tvoid = ++ctx.id;
	ctx.types.tvec4 = ++ctx.id;
	ctx.types.tbool = ++ctx.id;

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

	std::vector<u32> interface;
	for(auto& output : ctx.outputs) {
		interface.push_back(output.id);
	}

	write(buf, spv::OpEntryPoint, spv::ExecutionModelFragment,
		ctx.idmain, "main", interface);
	write(buf, spv::OpExecutionMode, ctx.idmain, spv::ExecutionModeOriginUpperLeft);

	std::vector<u32> sec8; // annotations (decorations)
	std::vector<u32> sec9; // types, constants

	write(sec9, spv::OpTypeFloat, ctx.types.tf32, 32);
	write(sec9, spv::OpTypeVoid, ctx.types.tvoid);
	write(sec9, spv::OpTypeVector, ctx.types.tvec4, ctx.types.tf32, 4);
	write(sec9, spv::OpTypeBool, ctx.types.tbool);
	write(sec9, spv::OpTypeFunction, ctx.idmaintype, ctx.types.tvoid);

	write(sec9, spv::OpConstantTrue, ctx.types.tbool, ctx.idtrue);
	write(sec9, spv::OpConstantFalse, ctx.types.tbool, ctx.idfalse);

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

	buf[maxboundid] = ctx.id + 1;
	buf.insert(buf.end(), sec8.begin(), sec8.end());
	buf.insert(buf.end(), sec9.begin(), sec9.end());
	buf.insert(buf.end(), ctx.buf.begin(), ctx.buf.end());
	return buf;
}
