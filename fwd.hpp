#pragma once

#include <cstdint>
#include <variant>
#include <string_view>
#include <vector>

// AST
struct List;
struct Definition;
struct Identifier;
struct Expression;

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
	std::variant<double, std::string_view, List, Identifier> value;
	Location loc;
};

struct Definition {
	std::string_view name;
	Expression expression;
};

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

[[noreturn]] void throwError(std::string msg, const Location& loc);

// Codegen
using u32 = std::uint32_t;

struct GenExpr {
	u32 id;
	u32 idtype;
	Type type;
};

struct Codegen {
	std::vector<u32> buf; // generated spirv body
	u32 id {}; // counter

	// reserved ids
	u32 idmain;
	u32 idmaintype;
	u32 idglsl;

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

void init(Codegen& ctx);
GenExpr generate(Codegen& ctx, const Expression& expr,
		const std::vector<Definition>& defs);
std::vector<u32> finish(Codegen& ctx);
