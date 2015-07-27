#ifndef PARTITIONING_HANDLER_H_
#define PARTITIONING_HANDLER_H_

#include <string>
#include <set>
#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

class PartitioningHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  PartitioningHandler() {};

  /// Callback whenever there's a match
  virtual void run(const clang::ast_matchers::MatchFinder::MatchResult & t_result) override;

 private:
  /// Convenience typedef: A vector of instructions, in our hyper-restrictive
  /// language everything is a BinaryOperator
  typedef std::vector<const clang::BinaryOperator*> InstructionVector;

  /// Convenience typedef: A map from BinaryOperator* to timestamp
  /// telling us when each BinaryOperator * is scheduled
  typedef std::map<const clang::BinaryOperator*, uint32_t> InstructionPartitioning;

  /// Does operation read variable?
  bool op_reads_var(const clang::BinaryOperator * op, const clang::Expr * var) const;

  /// Is there a dependence _from_ BinaryOperator op1 _to_ BinaryOperator op2?
  /// Returns true if op1 MUST precede op 2
  bool depends(const clang::BinaryOperator * op1, const clang::BinaryOperator * op2) const;

  /// Core partitioning logic to generate a pipeline
  /// Build a dag of dependencies and then schedule using the DAG
  InstructionPartitioning partition_into_pipeline(const InstructionVector & inst_vector) const;

  /// Check for pipeline-wide stateful variables
  /// Return a vector of strings that represent all
  /// variables that are pipeline-wide, and _require_ recirculation,
  /// in the absence of packed-word instructions.
  bool check_for_pipeline_vars(const InstructionPartitioning & partitioning) const;

  /// Convenience function to check if the expr
  /// is either a DeclRefExpr (state variable) or a
  /// a MemberExpr (packet variable)
  auto check_expr_type(const clang::Expr * expr) const { return clang::isa<clang::MemberExpr>(expr)
                                                                or clang::isa<clang::DeclRefExpr>(expr); }
};

#endif  // PARTITIONING_HANDLER_H_
