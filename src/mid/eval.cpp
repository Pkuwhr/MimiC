#include "mid/eval.h"

#include <cassert>

using namespace mimic::mid;
using namespace mimic::define;

namespace {

// create a new integer AST
inline ASTPtr MakeAST(std::uint32_t val, const ASTPtr &ast) {
  auto ret = std::make_unique<IntAST>(val);
  ret->set_logger(ast->logger());
  // check if is right value
  const auto &type = ast->ast_type();
  if (type->IsRightValue()) {
    ret->set_ast_type(type);
  }
  else {
    ret->set_ast_type(type->GetValueType(true));
  }
  return ret;
}

}  // namespace

xstl::Guard Evaluator::NewEnv() {
  values_ = xstl::MakeNestedMap(values_);
  return xstl::Guard([this] { values_ = values_->outer(); });
}

std::optional<std::uint32_t> Evaluator::EvalOn(VarDeclAST &ast) {
  // evaluate constant integers only
  const auto &type = ast.type()->ast_type();
  if (!type->IsConst() || !type->IsInteger()) return {};
  // evaluate definitions
  for (const auto &i : ast.defs()) i->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(VarDefAST &ast) {
  // do not evaluate array
  if (!ast.arr_lens().empty()) return {};
  // evaluate initial value
  if (!ast.init()) return {};
  auto val = ast.init()->Eval(*this);
  if (!val) return {};
  // add to environment
  values_->AddItem(ast.id(), val);
  // update AST
  ast.set_init(MakeAST(*val, ast.init()));
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(InitListAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(FuncDeclAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(FuncDefAST &ast) {
  ast.body()->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(FuncParamAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(StructDefAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(EnumDefAST &ast) {
  last_enum_val_ = 0;
  // evaluate all element definitions in enumeration
  for (const auto &i : ast.elems()) i->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(TypeAliasAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(StructElemAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(EnumElemAST &ast) {
  // check if has initial expression
  if (ast.expr()) {
    auto val = ast.expr()->Eval(*this);
    assert(val);
    last_enum_val_ = *val;
    // update AST
    ast.set_expr(MakeAST(*val, ast.expr()));
  }
  // add to environment
  values_->AddItem(ast.id(), last_enum_val_++);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(BlockAST &ast) {
  NewEnv();
  for (const auto &i : ast.stmts()) i->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(IfElseAST &ast) {
  ast.then()->Eval(*this);
  if (ast.else_then()) ast.else_then()->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(WhileAST &ast) {
  ast.body()->Eval(*this);
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(ControlAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(BinaryAST &ast) {
  using Op = BinaryAST::Operator;
  // do not evaluate binary expressions with assign operators
  // TODO: a little tricky, not reliable, fixme?
  if (static_cast<int>(ast.op()) >= static_cast<int>(Op::Assign)) {
    return {};
  }
  // evaluate lhs & rhs
  auto lhs = ast.lhs()->Eval(*this), rhs = ast.rhs()->Eval(*this);
  if (lhs) ast.set_lhs(MakeAST(*lhs, ast.lhs()));
  if (rhs) ast.set_rhs(MakeAST(*rhs, ast.rhs()));
  if (!lhs || !rhs) return {};
  // calculate result
  auto lv = *lhs, rv = *rhs;
  auto slv = static_cast<std::int32_t>(lv);
  auto srv = static_cast<std::int32_t>(rv);
  const auto &type = ast.ast_type();
  switch (ast.op()) {
    case Op::Add: return lv + rv;
    case Op::Sub: return lv - rv;
    case Op::Mul: return type->IsUnsigned() ? lv * rv : slv * srv;
    case Op::Div: return type->IsUnsigned() ? lv / rv : slv / srv;
    case Op::Mod: return type->IsUnsigned() ? lv % rv : slv % srv;
    case Op::And: return lv & rv;
    case Op::Or: return lv | rv;
    case Op::Xor: return lv ^ rv;
    case Op::Shl: return lv << rv;
    case Op::Shr: return type->IsUnsigned() ? lv >> rv : slv >> srv;
    case Op::LAnd: return lv && rv;
    case Op::LOr: return lv || rv;
    case Op::Equal: return lv == rv;
    case Op::NotEqual: return lv != rv;
    case Op::Less: return type->IsUnsigned() ? lv < rv : slv < srv;
    case Op::LessEq: return type->IsUnsigned() ? lv <= rv : slv <= srv;
    case Op::Great: return type->IsUnsigned() ? lv > rv : slv > srv;
    case Op::GreatEq: return type->IsUnsigned() ? lv >= rv : slv >= srv;
    default: assert(false); return {};
  }
}

std::optional<std::uint32_t> Evaluator::EvalOn(CastAST &ast) {
  // evaluate expression
  auto expr = ast.expr()->Eval(*this);
  if (!expr) return {};
  ast.set_expr(MakeAST(*expr, ast.expr()));
  // perform type casting
  auto val = *expr;
  const auto &type = ast.type()->ast_type();
  if (type->IsInteger() || type->IsPointer()) {
    if (type->GetSize() == 1) {
      // i8/u8
      return type->IsUnsigned() ? static_cast<std::uint8_t>(val)
                                : static_cast<std::int8_t>(val);
    }
    else if (type->GetSize() == 4) {
      // i32/u32/ptrs
      return type->IsUnsigned() || type->IsPointer()
                 ? static_cast<std::uint32_t>(val)
                 : static_cast<std::int32_t>(val);
    }
  }
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(UnaryAST &ast) {
  using Op = UnaryAST::Operator;
  // evaluate operand
  auto opr = ast.opr()->Eval(*this);
  if (!opr) return {};
  ast.set_opr(MakeAST(*opr, ast.opr()));
  // calculate result
  auto val = *opr;
  switch (ast.op()) {
    case Op::Pos: return +val;
    case Op::Neg: return -val;
    case Op::Not: return ~val;
    case Op::LNot: return !val;
    case Op::Deref: return {};
    case Op::Addr: return {};
    case Op::SizeOf: return ast.opr()->ast_type()->GetSize();
    default: assert(false); return {};
  }
}

std::optional<std::uint32_t> Evaluator::EvalOn(IndexAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(FuncCallAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(AccessAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(IntAST &ast) {
  return ast.value();
}

std::optional<std::uint32_t> Evaluator::EvalOn(CharAST &ast) {
  return ast.c();
}

std::optional<std::uint32_t> Evaluator::EvalOn(StringAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(IdAST &ast) {
  // query the environment and return the requested value if possible
  return values_->GetItem(ast.id());
}

std::optional<std::uint32_t> Evaluator::EvalOn(PrimTypeAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(StructTypeAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(EnumTypeAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(ConstTypeAST &ast) {
  return {};
}

std::optional<std::uint32_t> Evaluator::EvalOn(PointerTypeAST &ast) {
  return {};
}
