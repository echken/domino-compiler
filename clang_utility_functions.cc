#include "clang_utility_functions.h"

#include <iostream>

#include "llvm/Support/raw_ostream.h"
#include "clang/Basic/LangOptions.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"

#include "third_party/assert_exception.h"
#include "set_idioms.h"

using namespace clang;

std::string clang_stmt_printer(const clang::Stmt * stmt) {
  // Required for pretty printing
  clang::LangOptions LangOpts;
  LangOpts.CPlusPlus = true;
  clang::PrintingPolicy Policy(LangOpts);

  std::string str;
  llvm::raw_string_ostream rso(str);
  stmt->printPretty(rso, nullptr, Policy);
  return str;
}

std::string clang_value_decl_printer(const clang::ValueDecl * value_decl) {
  // Required for pretty printing
  clang::LangOptions LangOpts;
  LangOpts.CPlusPlus = true;
  clang::PrintingPolicy Policy(LangOpts);

  std::string str;
  llvm::raw_string_ostream rso(str);
  value_decl->printName(rso);
  return str;
}

std::string clang_decl_printer(const clang::Decl * decl) {
  // Required for pretty printing
  clang::LangOptions LangOpts;
  LangOpts.CPlusPlus = true;
  clang::PrintingPolicy Policy(LangOpts);

  std::string str;
  llvm::raw_string_ostream rso(str);
  decl->print(rso);
  return str;
}

bool is_packet_func(const clang::FunctionDecl * func_decl) {
  // Not sure what we would get out of functions with zero args
  assert_exception(func_decl->getNumParams() >= 1);
  return func_decl->getNumParams() == 1
         and func_decl->getParamDecl(0)->getType().getAsString() == "struct Packet";
}

std::set<std::string> identifier_census(const clang::TranslationUnitDecl * decl, const VariableTypeSelector & var_selector) {
  std::set<std::string> identifiers = {};
  assert_exception(decl != nullptr);

  // Get all decls by dyn casting decl into a DeclContext
  for (const auto * child_decl : dyn_cast<DeclContext>(decl)->decls()) {
    assert_exception(child_decl);
    assert_exception(child_decl->isDefinedOutsideFunctionOrMethod());
    if (isa<RecordDecl>(child_decl)) {
      // add current fields in struct to identifiers
      if (var_selector.at(VariableType::PACKET)) {
        for (const auto * field_decl : dyn_cast<DeclContext>(child_decl)->decls())
          identifiers.emplace(dyn_cast<FieldDecl>(field_decl)->getName());
      }
    } else if (isa<FunctionDecl>(child_decl)) {
      if (var_selector.at(VariableType::FUNCTION_PARAMETER)) {
        // add function name
        identifiers.emplace(dyn_cast<FunctionDecl>(child_decl)->getName());
        // add all function parameters
        for (const auto * parm_decl : dyn_cast<FunctionDecl>(child_decl)->parameters()) {
          identifiers.emplace(dyn_cast<ParmVarDecl>(parm_decl)->getName());
        }
      }
    } else if (isa<ValueDecl>(child_decl)) {
      // add state variable name
      if (var_selector.at(VariableType::STATE)) {
        identifiers.emplace(dyn_cast<ValueDecl>(child_decl)->getName());
      }
    } else {
      // We can't remove TypedefDecl from the AST for some reason.
      assert_exception(isa<TypedefDecl>(child_decl));
    }
  }
  return identifiers;
}

std::set<std::string> gen_var_list(const Stmt * stmt, const VariableTypeSelector & var_selector) {
  // Recursively scan stmt to generate a set of strings representing
  // either packet fields or state variables used within stmt
  assert_exception(stmt);
  std::set<std::string> ret;
  if (isa<CompoundStmt>(stmt)) {
    for (const auto & child : stmt->children()) {
      ret = ret + gen_var_list(child, var_selector);
    }
    return ret;
  } else if (isa<IfStmt>(stmt)) {
    const auto * if_stmt = dyn_cast<IfStmt>(stmt);
    if (if_stmt->getElse() != nullptr) {
      return gen_var_list(if_stmt->getCond(), var_selector) + gen_var_list(if_stmt->getThen(), var_selector) + gen_var_list(if_stmt->getElse(), var_selector);
    } else {
      return gen_var_list(if_stmt->getCond(), var_selector) + gen_var_list(if_stmt->getThen(), var_selector);
    }
  } else if (isa<BinaryOperator>(stmt)) {
    const auto * bin_op = dyn_cast<BinaryOperator>(stmt);
    return gen_var_list(bin_op->getLHS(), var_selector) + gen_var_list(bin_op->getRHS(), var_selector);
  } else if (isa<ConditionalOperator>(stmt)) {
    const auto * cond_op = dyn_cast<ConditionalOperator>(stmt);
    return gen_var_list(cond_op->getCond(), var_selector) + gen_var_list(cond_op->getTrueExpr(), var_selector) + gen_var_list(cond_op->getFalseExpr(), var_selector);
  } else if (isa<MemberExpr>(stmt)) {
    return (var_selector.at(VariableType::PACKET))
           ? std::set<std::string>{clang_stmt_printer(stmt)}
           : std::set<std::string>();
  } else if (isa<DeclRefExpr>(stmt) or isa<ArraySubscriptExpr>(stmt)) {
    return (var_selector.at(VariableType::STATE))
           ? std::set<std::string>{clang_stmt_printer(stmt)}
           : std::set<std::string>();
  } else if (isa<IntegerLiteral>(stmt)) {
    return std::set<std::string>();
  } else if (isa<ParenExpr>(stmt)) {
    return gen_var_list(dyn_cast<ParenExpr>(stmt)->getSubExpr(), var_selector);
  } else if (isa<UnaryOperator>(stmt)) {
    const auto * un_op = dyn_cast<UnaryOperator>(stmt);
    assert_exception(un_op->isArithmeticOp());
    const auto opcode_str = std::string(UnaryOperator::getOpcodeStr(un_op->getOpcode()));
    assert_exception(opcode_str == "!");
    return gen_var_list(un_op->getSubExpr(), var_selector);
  } else if (isa<ImplicitCastExpr>(stmt)) {
    return gen_var_list(dyn_cast<ImplicitCastExpr>(stmt)->getSubExpr(), var_selector);
  } else if (isa<CallExpr>(stmt)) {
    const auto * call_expr = dyn_cast<CallExpr>(stmt);
    std::set<std::string> ret;
    for (const auto * child : call_expr->arguments()) {
      const auto child_uses = gen_var_list(child, var_selector);
      ret = ret + child_uses;
    }
    return ret;
  } else {
    throw std::logic_error("gen_var_list cannot handle stmt of type " + std::string(stmt->getStmtClassName()));
  }
}

std::string generate_scalar_func_def(const FunctionDecl * func_decl) {
  // Yet another C quirk. Adding a semicolon after a function definition
  // is caught by -pedantic, not adding a semicolon after a function declaration
  // without a definition is not permitted in C :)
  assert_exception(func_decl);
  assert_exception(not is_packet_func(func_decl));
  const bool has_body = func_decl->hasBody();
  return clang_decl_printer(func_decl) + (has_body ? "" : ";");
}

std::string gen_pkt_fields(const TranslationUnitDecl * tu_decl) {
  std::string ret = "";
  for (const auto & field : identifier_census(tu_decl,
                                              {{VariableType::PACKET, true},
                                               {VariableType::STATE,  false},
                                               {VariableType::FUNCTION_PARAMETER, false}})) {
    ret += field + "\n";
  }
  return ret;
}
