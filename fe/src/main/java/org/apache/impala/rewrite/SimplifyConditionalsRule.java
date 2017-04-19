// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package org.apache.impala.rewrite;

import java.util.ArrayList;
import java.util.List;

import org.apache.impala.analysis.Analyzer;
import org.apache.impala.analysis.BinaryPredicate;
import org.apache.impala.analysis.BoolLiteral;
import org.apache.impala.analysis.CaseExpr;
import org.apache.impala.analysis.CaseWhenClause;
import org.apache.impala.analysis.CompoundPredicate;
import org.apache.impala.analysis.Expr;
import org.apache.impala.analysis.FunctionCallExpr;
import org.apache.impala.analysis.FunctionName;
import org.apache.impala.analysis.NullLiteral;
import org.apache.impala.common.AnalysisException;
import com.google.common.base.Preconditions;

/***
 * This rule simplifies conditional functions with constant conditions. It relies on
 * FoldConstantsRule to replace the constant conditions with a BoolLiteral or NullLiteral
 * first, and on NormalizeExprsRule to normalize CompoundPredicates.
 *
 * Examples:
 * if (true, 0, 1) -> 0
 * id = 0 OR false -> id = 0
 * false AND id = 1 -> false
 * case when false then 0 when true then 1 end -> 1
 */
public class SimplifyConditionalsRule implements ExprRewriteRule {
  public static ExprRewriteRule INSTANCE = new SimplifyConditionalsRule();

  @Override
  public Expr apply(Expr expr, Analyzer analyzer) throws AnalysisException {
    if (!expr.isAnalyzed()) return expr;

    if (expr instanceof FunctionCallExpr) {
      return simplifyFunctionCallExpr((FunctionCallExpr) expr);
    } else if (expr instanceof CompoundPredicate) {
      return simplifyCompoundPredicate((CompoundPredicate) expr);
    } else if (expr instanceof CaseExpr) {
      return simplifyCaseExpr((CaseExpr) expr, analyzer);
    }
    return expr;
  }

  /**
   * Simplifies IF by returning the corresponding child if the condition has a constant
   * TRUE, FALSE, or NULL (equivalent to FALSE) value.
   */
  private Expr simplifyFunctionCallExpr(FunctionCallExpr expr) {
    FunctionName fnName = expr.getFnName();

    // TODO: Add the other conditional functions, eg. ifnull, istrue, etc.
    if (fnName.getFunction().equals("if")) {
      Preconditions.checkState(expr.getChildren().size() == 3);
      if (expr.getChild(0) instanceof BoolLiteral) {
        if (((BoolLiteral) expr.getChild(0)).getValue()) {
          // IF(TRUE)
          return expr.getChild(1);
        } else {
          // IF(FALSE)
          return expr.getChild(2);
        }
      } else if (expr.getChild(0) instanceof NullLiteral) {
        // IF(NULL)
        return expr.getChild(2);
      }
    }
    return expr;
  }

  /**
   * Simplifies compound predicates with at least one BoolLiteral child, which
   * NormalizeExprsRule ensures will be the left child,  according to the following rules:
   * true AND 'expr' -> 'expr'
   * false AND 'expr' -> false
   * true OR 'expr' -> true
   * false OR 'expr' -> 'expr'
   *
   * Unlike other rules here such as IF, we cannot in general simplify CompoundPredicates
   * with a NullLiteral child (unless the other child is a BoolLiteral), eg. null and
   * 'expr' is false if 'expr' is false but null if 'expr' is true.
   *
   * NOT is covered by FoldConstantRule.
   */
  private Expr simplifyCompoundPredicate(CompoundPredicate expr) {
    Expr leftChild = expr.getChild(0);
    if (!(leftChild instanceof BoolLiteral)) return expr;

    if (expr.getOp() == CompoundPredicate.Operator.AND) {
      if (((BoolLiteral) leftChild).getValue()) {
        // TRUE AND 'expr', so return 'expr'.
        return expr.getChild(1);
      } else {
        // FALSE AND 'expr', so return FALSE.
        return leftChild;
      }
    } else if (expr.getOp() == CompoundPredicate.Operator.OR) {
      if (((BoolLiteral) leftChild).getValue()) {
        // TRUE OR 'expr', so return TRUE.
        return leftChild;
      } else {
        // FALSE OR 'expr', so return 'expr'.
        return expr.getChild(1);
      }
    }
    return expr;
  }

  /**
   * Simpilfies CASE and DECODE. If any of the 'when's have constant FALSE/NULL values,
   * they are removed. If all of the 'when's are removed, just the ELSE is returned. If
   * any of the 'when's have constant TRUE values, the leftmost one becomes the ELSE
   * clause and all following cases are removed.
   */
  private Expr simplifyCaseExpr(CaseExpr expr, Analyzer analyzer)
      throws AnalysisException {
    Expr caseExpr = expr.hasCaseExpr() ? expr.getChild(0) : null;
    if (expr.hasCaseExpr() && !caseExpr.isLiteral()) return expr;

    int numChildren = expr.getChildren().size();
    int loopStart = expr.hasCaseExpr() ? 1 : 0;
    // Check and return early if there's nothing that can be simplified.
    boolean canSimplify = false;
    for (int i = loopStart; i < numChildren - 1; i += 2) {
      if (expr.getChild(i).isLiteral()) {
        canSimplify = true;
        break;
      }
    }
    if (!canSimplify) return expr;

    // Contains all 'when' clauses with non-constant conditions, used to construct the new
    // CASE expr while removing any FALSE or NULL cases.
    List<CaseWhenClause> newWhenClauses = new ArrayList<CaseWhenClause>();
    // Set to THEN of first constant TRUE clause, if any.
    Expr elseExpr = null;
    for (int i = loopStart; i < numChildren - 1; i += 2) {
      Expr child = expr.getChild(i);
      if (child instanceof NullLiteral) continue;

      Expr whenExpr;
      if (expr.hasCaseExpr()) {
        if (child.isLiteral()) {
          BinaryPredicate pred = new BinaryPredicate(
              BinaryPredicate.Operator.EQ, caseExpr, expr.getChild(i));
          pred.analyze(analyzer);
          whenExpr = analyzer.getConstantFolder().rewrite(pred, analyzer);
        } else {
          whenExpr = null;
        }
      } else {
        whenExpr = child;
      }

      if (whenExpr instanceof BoolLiteral) {
        if (((BoolLiteral) whenExpr).getValue()) {
          if (newWhenClauses.size() == 0) {
            // This WHEN is always TRUE, and any cases preceding it are constant
            // FALSE/NULL, so just return its THEN.
            return expr.getChild(i + 1).castTo(expr.getType());
          } else {
            // This WHEN is always TRUE, so the cases after it can never be reached.
            elseExpr = expr.getChild(i + 1);
            break;
          }
        } else {
          // This WHEN is always FALSE, so it can be removed.
        }
      } else {
        newWhenClauses.add(new CaseWhenClause(child, expr.getChild(i + 1)));
      }
    }

    if (expr.hasElseExpr() && elseExpr == null) elseExpr = expr.getChild(numChildren - 1);
    if (newWhenClauses.size() == 0) {
      // All of the WHEN clauses were FALSE, return the ELSE.
      if (elseExpr == null) return NullLiteral.create(expr.getType());
      return elseExpr;
    }
    return new CaseExpr(caseExpr, newWhenClauses, elseExpr);
  }
}
