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

struct BackEdge {
	u32 block;
	std::vector<u32> params;
};

struct RecData {
	u32 header;
	u32 cont;

	std::vector<u32> paramTypes;
	std::vector<BackEdge> loops;
};

struct RecContext : public Context {
	RecData* rec {}; // information about the deepest level of rec-func
};

struct CallArgs {
	const std::vector<CExpression>* values;
	const Defs* defs;
};

// Recursive generation
GenExpr generateCall(const RecContext& ctx, const CExpression& expr,
		const std::vector<CallArgs>& args);
GenExpr generate(const RecContext& ctx, const CExpression& expr);

using BuiltinGen = GenExpr(*)(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args);

GenExpr generateIf(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.empty()) {
		throwError("Invalid call nesting", loc);
	}

	if(args.back().values->size() != 4) {
		throwError("'if' needs 3 arguments", loc);
	}

	auto cond = generate(ctx, (*args.back().values)[1]);
	if(cond.idtype != ctx.codegen.types.tbool) {
		throwError("'if' condition (first arg) must be bool", loc);
	}

	auto nargs = args;
	nargs.pop_back();

	auto tlabel = ++ctx.codegen.id;
	auto flabel = ++ctx.codegen.id;
	auto dstlabel = ++ctx.codegen.id;

	auto& buf = ctx.codegen.buf;
	write(buf, spv::OpSelectionMerge, dstlabel, spv::SelectionControlMaskNone);
	write(buf, spv::OpBranchConditional, cond.id, tlabel, flabel);

	auto nctx = RecContext {ctx.codegen, *args.back().defs, ctx.rec};

	// true label
	write(buf, spv::OpLabel, tlabel);
	ctx.codegen.block = tlabel;
	auto et = generateCall(nctx, (*args.back().values)[2], nargs);

	auto ptt = std::get_if<PrimitiveType>(&et.type);
	auto rt = ptt && *ptt == PrimitiveType::eRecCall;
	if(!rt) {
		write(buf, spv::OpBranch, dstlabel);
	}

	// false label
	write(buf, spv::OpLabel, flabel);
	ctx.codegen.block = flabel;
	auto ef = generateCall(nctx, (*args.back().values)[3], nargs);

	auto ptf = std::get_if<PrimitiveType>(&ef.type);
	auto rf = ptf && *ptf == PrimitiveType::eRecCall;
	if(!rf) {
		write(buf, spv::OpBranch, dstlabel);
	}

	// dst block
	if(!rf || !rt) {
		write(buf, spv::OpLabel, dstlabel);
		ctx.codegen.block = dstlabel;
	}

	if(!rf && !rt) {
		if(et.idtype != ef.idtype) {
			throwError("if branches have different types", loc);
		}

		// otherwise we need phi instruction
		auto phi = ++ctx.codegen.id;
		write(buf, spv::OpPhi, et.idtype, phi,
			et.id, tlabel, ef.id, flabel);

		return {phi, et.idtype, et.type};

	// if either type is RecCall, we can make it work
	// TODO: we should propagate it if only one of the types
	// is eRecCall. At the moment this is a silent error!
	// Include a 'usable' or 'rec' flag in type.
	} else if(rf) {
		return {et.id, et.idtype, et.type};
	} else if(rt) {
		return {ef.id, ef.idtype, ef.type};
	}

	// otherwise: both branches are rec calls
	return {0, 0, PrimitiveType::eRecCall};
}

template<spv::Op Op>
GenExpr generateBinop(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0].values->size() != 3) {
		std::string msg = "binop";
		msg += " expects 2 arguments";
		throwError(msg, loc);
	}

	auto nctx = RecContext {ctx.codegen, *args.back().defs, ctx.rec};
	auto e1 = generate(nctx, (*args[0].values)[1]);
	auto e2 = generate(nctx, (*args[0].values)[2]);
	if(e1.idtype != e2.idtype) {
		std::string msg = "binop";
		msg += " arguments must have same type";
		throwError(msg, loc);
	}

	auto oid = ++ctx.codegen.id;
	write(ctx.codegen.buf, Op, e1.idtype, oid, e1.id, e2.id);
	return {oid, e1.idtype, e1.type};
}

GenExpr generateVec4(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0].values->size() != 5) {
		throwError("vec4 expects 4 arguments", loc);
	}

	auto nctx = RecContext {ctx.codegen, *args.back().defs, ctx.rec};

	std::vector<u32> ids;
	auto e1 = generate(nctx, (*args[0].values)[1]);
	auto e2 = generate(nctx, (*args[0].values)[2]);
	auto e3 = generate(nctx, (*args[0].values)[3]);
	auto e4 = generate(nctx, (*args[0].values)[4]);

	// TODO: check that all types are floats

	auto oid = ++ctx.codegen.id;
	write(ctx.codegen.buf, spv::OpCompositeConstruct, ctx.codegen.types.tvec4,
		oid, e1.id, e2.id, e3.id, e4.id);

	auto type = VectorType{4, PrimitiveType::eFloat};
	return {oid, ctx.codegen.types.tvec4, type};
}

GenExpr generateOutput(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0].values->size() != 3) {
		throwError("output expects 2 arguments", loc);
	}

	auto& a1 = (*args[0].values)[1];
	auto oloc = std::get_if<double>(&a1.value);
	if(!oloc) {
		throwError("First argument of output must be int", a1.loc);
	}

	auto nctx = RecContext {ctx.codegen, *args.back().defs, ctx.rec};
	auto e1 = generate(nctx, (*args[0].values)[2]);

	auto oid = ++ctx.codegen.id;
	ctx.codegen.outputs.push_back({oid, u32(*oloc), e1.idtype});

	write(ctx.codegen.buf, spv::OpStore, oid, e1.id);
	return {0, 0, PrimitiveType::eVoid};
}

GenExpr generateLet(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.empty()) {
		throwError("Invalid call nesting", loc);
	}

	if(args.back().values->size() != 3) {
		throwError("let expects two arguments", loc);
	}

	auto lets = std::get_if<List>(&(*args.back().values)[1].value);
	if(!lets) {
		throwError("first parameter of let must be list",
			(*args.back().values)[1].loc);
	}

	auto ndefs = ctx.defs;
	for(auto& def : lets->values) {
		auto list = std::get_if<List>(&def.value);
		if(!list || list->values.size() != 2) {
			throwError("bindings in let must be (identifier expr) pairs",
				def.loc);
		}

		auto identifier = std::get_if<Identifier>(&list->values[0].value);
		if(!identifier) {
			throwError("bindings in let must be (identifier expr) pairs",
				list->values[0].loc);
		}

		auto de = DefExpr{wrap(list->values[1]), &ctx.defs};
		ndefs.insert_or_assign(identifier->name, de);
	}

	auto nctx = RecContext{ctx.codegen, ndefs, ctx.rec};

	auto nargs = args;
	nargs.pop_back();

	auto& body = (*args.back().values)[2];
	return generateCall(nctx, body, nargs);
}

GenExpr generateEq(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(args.size() != 1) {
		throwError("Invalid call nesting", loc);
	}

	if(args[0].values->size() != 3) {
		std::string msg = "eq expects 2 arguments";
		throwError(msg, loc);
	}

	auto nctx = RecContext {ctx.codegen, *args.back().defs, ctx.rec};
	auto e1 = generate(nctx, (*args[0].values)[1]);
	auto e2 = generate(nctx, (*args[0].values)[2]);
	if(e1.idtype != e2.idtype || e1.idtype != ctx.codegen.types.tf32) {
		std::string msg = "eq arguments must have same type";
		throwError(msg, loc);
	}

	auto oid = ++ctx.codegen.id;
	write(ctx.codegen.buf, spv::OpFOrdEqual, ctx.codegen.types.tbool,
		oid, e1.id, e2.id);
	return {oid, ctx.codegen.types.tbool, PrimitiveType::eBool};
}

GenExpr generateRec(const RecContext& ctx, const Location& loc,
		const std::vector<CallArgs>& args) {
	if(!ctx.rec) {
		throwError("rec can only appear in rec-func", loc);
	}

	// NOTE: this is where it becomes apparent that we can't return
	// first-class function values from a recursive function:
	// inlining simply fails
	if(args.size() != 1) {
		std::string msg = "Invalid call nesting";
		msg += " (recursive functions can't return function objects)";
		throwError(msg, loc);
	}

	auto& cargs = *args[0].values;
	if(cargs.size() != ctx.rec->paramTypes.size() + 1) {
		throwError("rec: invalid number of parameters", loc);
	}

	auto& cg = ctx.codegen;
	BackEdge edge;
	edge.block = cg.block;

	auto nctx = RecContext {ctx.codegen, *args[0].defs, ctx.rec};
	for(auto i = 0u; i < cargs.size() - 1; ++i) {
		// NOTE: this is where it becomes apparent that
		// we can't pass functions (as first-class
		// parameters) to a recursive function.
		// Trying to generate instructions for a function
		// will fail.
		auto& param = cargs[i + 1];
		auto e = generate(nctx, param);
		if(e.id == 0) {
			throwError("Invalid parameter expr", param.loc);
		}

		if(e.idtype != ctx.rec->paramTypes[i]) {
			throwError("Type of argument must match initial type",
				param.loc);
		}

		dlg_assert(e.id != 0);
		edge.params.push_back(e.id);
	}

	ctx.rec->loops.push_back(edge);
	write(cg.buf, spv::OpBranch, ctx.rec->cont);
	return {0, 0, PrimitiveType::eRecCall};
}

const std::unordered_map<std::string_view, BuiltinGen> builtins = {
	{"if", generateIf},
	{"let", generateLet},
	{"rec", generateRec},
	{"output", generateOutput},
	{"+", &generateBinop<spv::OpFAdd>},
	{"-", generateBinop<spv::OpFSub>},
	{"*", generateBinop<spv::OpFMul>},
	{"/", generateBinop<spv::OpFDiv>},
	{"vec4", generateVec4},
	{"eq", generateEq},
};

const static Defs emptyDefs = {};
GenExpr generateCall(const RecContext& ctx, const List& list,
		const Location& loc, const std::vector<CallArgs>& args) {
	auto& cg = ctx.codegen;
	if(list.values.size() > 1 &&
			std::holds_alternative<Identifier>(list.values[0].value)) {
		auto name = std::get<Identifier>(list.values[0].value).name;

		// special application case: function definition
		if(name == "func" || name == "rec-func") {
			auto nargs = args;
			if(list.values.size() != 3) {
				throwError("Invalid function definition (value count)", loc);
			}

			// function arguments
			auto pfargs = std::get_if<List>(&list.values[1].value);
			if(!pfargs) {
				throwError("Invalid function definition (param)", loc);
			}

			auto& fargs = pfargs->values;
			if(args.empty()) {
				throwError("Function call without params", loc);
			}

			// call arguments
			auto& cargs = *args.back().values;
			if(fargs.size() + 1 != cargs.size()) {
				auto msg = dlg::format("Function call with invalid number "
					"of params: Expected {}, got {}", fargs.size(),
					args.back().values->size());
				throwError(msg, loc);
			}

			auto& body = list.values[2];

			// allow (tail-)recursion
			if(name == "rec-func") {
				// generate blocks
				auto hb = ++cg.id; // header block
				auto lb = ++cg.id; // first loop block
				auto cb = ++cg.id; // continue block
				auto mb = ++cg.id; // merge block

				// generate parameters
				auto ndefs = ctx.defs;
				RecData rec;

				std::vector<u32> paramIDs;
				std::vector<u32> initIDs;
				for(auto i = 0u; i < fargs.size(); ++i) {
					// NOTE: this is where it becomes apparent that
					// we can't pass functions (as first-class
					// parameters) to a recursive function.
					// Trying to generate instructions for a function
					// will fail.
					auto& param = cargs[i + 1];

					auto nctx = RecContext{ctx.codegen, *args.back().defs, ctx.rec};
					auto e = generate(nctx, param);
					if(e.id == 0) {
						throwError("Invalid parameter expr", param.loc);
					}
					initIDs.push_back(e.id);

					auto paramID = ++cg.id;
					rec.paramTypes.push_back(e.idtype);
					paramIDs.push_back(paramID);

					auto name = std::get_if<Identifier>(&fargs[i].value);
					if(!name) {
						throwError("Invalid function definition (param identifier)",
							loc);
					}

					auto paramExpr = e;
					paramExpr.id = paramID;
					ndefs.insert_or_assign(name->name,
						DefExpr{{{paramExpr}, param.loc}, &emptyDefs});
				}

				// [header block]
				write(cg.buf, spv::OpBranch, hb);
				write(cg.buf, spv::OpLabel, hb);

				std::vector<u32> contPhis; // output ids of phis in cont block
				for(auto i = 0u; i < fargs.size(); ++i) {
					auto contID = ++cg.id;
					contPhis.push_back(contID);
					write(cg.buf, spv::OpPhi, rec.paramTypes[i], paramIDs[i],
						initIDs[i], cg.block, contID, cb);
				}

				nargs.pop_back();

				write(cg.buf, spv::OpLoopMerge, mb, cb, spv::LoopControlMaskNone);
				write(cg.buf, spv::OpBranch, lb);

				// [loop block]
				write(cg.buf, spv::OpLabel, lb);

				// insert function body
				rec.header = hb;
				rec.cont = cb;
				auto nctx = RecContext {cg, ndefs, &rec};
				cg.block = cb;

				auto ret = generateCall(nctx, wrap(body), nargs);
				write(cg.buf, spv::OpBranch, mb);

				// [continue block]
				write(cg.buf, spv::OpLabel, cb);
				for(auto i = 0u; i < fargs.size(); ++i) {
					std::vector<u32> phiParams;
					for(auto& back : rec.loops) {
						phiParams.push_back(back.params[i]);
						phiParams.push_back(back.block);
					}

					write(cg.buf, spv::OpPhi, rec.paramTypes[i],
						contPhis[i], phiParams);
				}

				write(cg.buf, spv::OpBranch, hb);

				// [merge block]
				write(cg.buf, spv::OpLabel, mb);
				cg.block = mb;
				return ret;
			} else {
				auto ndefs = ctx.defs;
				for(auto i = 0u; i < fargs.size(); ++i) {
					auto name = std::get_if<Identifier>(&fargs[i].value);
					if(!name) {
						throwError("Invalid function definition (param identifier)",
							loc);
					}

					ndefs.insert_or_assign(name->name,
						DefExpr{cargs[i + 1], args.back().defs});
				}

				nargs.pop_back();
				auto nctx = RecContext {cg, ndefs, ctx.rec};
				return generateCall(nctx, wrap(body), nargs);
			}
		}
	}

	auto nargs = args;
	std::vector<CExpression> cargs;
	for(auto& val : list.values) {
		cargs.push_back(wrap(val));
	}

	nargs.push_back({&cargs, &ctx.defs});
	return generateCall(ctx, cargs[0], nargs);
}

GenExpr generateCall(const RecContext& ctx, const Identifier& identifier,
		const Location& loc, const std::vector<CallArgs>& args) {
	auto fname = identifier.name;
	auto it = builtins.find(fname);
	if(it == builtins.end()) {
		auto it = ctx.defs.find(fname);
		if(it == ctx.defs.end()) {
			std::string msg = "Unknown function identifier '";
			msg += fname;
			msg += "'";
			throwError(msg, loc);
		}

		auto nctx = RecContext{ctx.codegen, *it->second.scope, ctx.rec};
		return generateCall(nctx, it->second.expr, args);
	} else {
		return it->second(ctx, loc, args);
	}
}

GenExpr generateCall(const RecContext& ctx, const CExpression& expr,
		const std::vector<CallArgs>& args) {
	if(args.empty()) {
		return generate(ctx, expr);
	}

	return std::visit(Visitor{
		[&](const List& list) { return generateCall(ctx, list, expr.loc, args); },
		[&](const Identifier& id) { return generateCall(ctx, id, expr.loc, args); },
		[&](const auto&) {
			throwError("Invalid application; no function", expr.loc);
			return GenExpr {};
		},
	}, expr.value);
}

GenExpr generate(const RecContext& ctx, const CExpression& expr) {
	auto& cg = ctx.codegen;

	return std::visit(Visitor{
		[&](double val) {
			// all constants have to be declared at the start of the program
			// and we therefore backpatch them to the start later on
			// constant number
			u32 v;
			float f = val;
			std::memcpy(&v, &f, 4);
			auto oid = ++cg.id;
			cg.constants.push_back({oid, v, cg.types.tf32});
			return GenExpr{oid, cg.types.tf32, PrimitiveType::eFloat};
		},
		[&](bool val) {
			u32 id = val ? cg.idtrue : cg.idfalse;
			return GenExpr{id, cg.types.tbool, PrimitiveType::eBool};
		},
		[&](const Identifier& id) {
			auto it = ctx.defs.find(id.name);
			if(it == ctx.defs.end()) {
				std::string msg = "Unknown identifier '";
				msg += id.name;
				msg += "'";
				throwError(msg, expr.loc);
			}

			auto nctx = RecContext{ctx.codegen, *it->second.scope, ctx.rec};
			return generate(nctx, it->second.expr);
		},
		[&](const GenExpr& ge) {
			return ge;
		},
		[&](std::string_view) {
			throwError("Can't generate string", expr.loc);
			return GenExpr {};
		},
		[&](const List& list) {
			std::vector<CExpression> args;
			for(auto& val : list.values) {
				args.push_back(wrap(val));
			}

			return generateCall(ctx, args[0], {{&args, &ctx.defs}});
		}
	}, expr.value);
}

GenExpr generateExpr(const Context& ctx, const Expression& expr) {
	RecContext rctx{ctx.codegen, ctx.defs, nullptr};
	return generate(rctx, wrap(expr));
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

	ctx.entryblock = ++ctx.id;
	ctx.block = ctx.entryblock;
	write(ctx.buf, spv::OpLabel, ctx.entryblock);
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
