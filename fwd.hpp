#pragma once

#include <cstdint>
#include <variant>
#include <string_view>
#include <vector>
#include <deque>
#include <unordered_map>

using u32 = std::uint32_t;
struct List;
struct Identifier;
struct Expression;
struct DefExpr;

using Defs = std::unordered_map<std::string_view, DefExpr>;

enum class PrimitiveType {
	eVoid,
	eFloat,
	eBool,
	eRecCall,
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
struct GenExpr {
	u32 id;
	u32 idtype;
	Type type;
};

// AST
struct Location {
	unsigned row {0};
	unsigned col {0};
};

struct List {
	std::vector<Expression> values;
};

struct Identifier {
	std::string_view name;
};

struct Expression {
	std::variant<double, std::string_view, List, Identifier, bool, GenExpr> value;
	Location loc;
};

struct DefExpr {
	Expression expr;
	const Defs* scope;
};

[[noreturn]] void throwError(std::string msg, const Location& loc);

// Codegen
struct Codegen {
	std::vector<u32> buf; // generated spirv body
	u32 id {}; // counter

	// reserved ids
	u32 idmain;
	u32 idmaintype;
	u32 idglsl;
	u32 entryblock;

	u32 idtrue;
	u32 idfalse;

	u32 block; // id of the current block

	struct {
		u32 tf32 {};
		u32 tvec4 {};
		u32 tvoid {};
		u32 tbool {};
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

struct Context {
	Codegen& codegen;
	const Defs& defs;
};

void init(Codegen& ctx);
GenExpr generateExpr(const Context& ctx, const Expression& expr);
std::vector<u32> finish(Codegen& ctx);
