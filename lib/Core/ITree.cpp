/*
 * ITree.cpp
 *
 *  Created on: Oct 15, 2015
 *      Author: felicia
 */

#include "ITree.h"
#include "TimingSolver.h"
#include "Dependency.h"

#include <klee/Expr.h>
#include <klee/Solver.h>
#include <klee/util/ExprPPrinter.h>
#include <vector>

using namespace klee;

// Interpolation is enabled by default
bool InterpolationOption::interpolation = true;

/**/

PathConditionMarker::PathConditionMarker(PathCondition *pathCondition)
    : mayBeInInterpolant(false), pathCondition(pathCondition) {}

PathConditionMarker::~PathConditionMarker() {}

void PathConditionMarker::mayIncludeInInterpolant() {
  mayBeInInterpolant = true;
}

void PathConditionMarker::includeInInterpolant(AllocationGraph *g) {
  if (mayBeInInterpolant) {
    pathCondition->includeInInterpolant(g);
  }
}

/**/

PathCondition::PathCondition(ref<Expr> &constraint, Dependency *dependency,
                             llvm::Value *condition, PathCondition *prev)
    : constraint(constraint), shadowConstraint(constraint), shadowed(false),
      dependency(dependency),
      condition(dependency ? dependency->getLatestValue(condition, constraint)
                           : 0),
      inInterpolant(false), tail(prev) {}

PathCondition::~PathCondition() {}

ref<Expr> PathCondition::car() const { return constraint; }

PathCondition *PathCondition::cdr() const { return tail; }

void PathCondition::includeInInterpolant(AllocationGraph *g) {
  // We mark all values to which this constraint depends
  dependency->markAllValues(g, condition);

  // We mark this constraint itself as in the interpolant
  inInterpolant = true;
}

bool PathCondition::carInInterpolant() const { return inInterpolant; }

ref<Expr>
PathCondition::packInterpolant(std::vector<const Array *> &replacements) {
  ref<Expr> res;
  for (PathCondition *it = this; it != 0; it = it->tail) {
    if (it->inInterpolant) {
      if (!it->shadowed) {
        it->shadowConstraint =
            ShadowArray::getShadowExpression(it->constraint, replacements);
        it->shadowed = true;
      }
      if (res.get()) {
        res = AndExpr::alloc(res, it->shadowConstraint);
      } else {
        res = it->shadowConstraint;
      }
    }
  }
  return res;
}

void PathCondition::dump() {
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void PathCondition::print(llvm::raw_ostream &stream) {
  stream << "[";
  for (PathCondition *it = this; it != 0; it = it->tail) {
    it->constraint->print(stream);
    stream << ": " << (it->inInterpolant ? "interpolant constraint"
                                         : "non-interpolant constraint");
    if (it->tail != 0)
      stream << ",";
  }
  stream << "]";
}

/**/

SubsumptionTableEntry::SubsumptionTableEntry(ITreeNode *node)
    : nodeId(node->getNodeId()) {
  std::vector<const Array *> replacements;

  interpolant = node->getInterpolant(replacements);

  singletonStore = node->getLatestInterpolantCoreExpressions(replacements);
  for (std::map<llvm::Value *, ref<Expr> >::iterator
           it = singletonStore.begin(),
           itEnd = singletonStore.end();
       it != itEnd; ++it) {
    singletonStoreKeys.push_back(it->first);
  }

  compositeStore = node->getCompositeInterpolantCoreExpressions(replacements);
  for (std::map<llvm::Value *, std::vector<ref<Expr> > >::iterator
           it = compositeStore.begin(),
           itEnd = compositeStore.end();
       it != itEnd; ++it) {
    compositeStoreKeys.push_back(it->first);
  }

  existentials = replacements;
}

SubsumptionTableEntry::~SubsumptionTableEntry() {}

bool
SubsumptionTableEntry::hasExistentials(std::vector<const Array *> &existentials,
                                       ref<Expr> expr) {
  for (int i = 0, numKids = expr->getNumKids(); i < numKids; ++i) {
    if (llvm::isa<ReadExpr>(expr)) {
      ReadExpr *readExpr = llvm::dyn_cast<ReadExpr>(expr.get());
      const Array *array = (readExpr->updates).root;
      for (std::vector<const Array *>::iterator it = existentials.begin(),
                                                itEnd = existentials.end();
           it != itEnd; ++it) {
        if ((*it) == array)
          return true;
      }
    } else if (hasExistentials(existentials, expr->getKid(i)))
      return true;
  }
  return false;
}

ref<Expr>
SubsumptionTableEntry::simplifyWithFourierMotzkin(ref<Expr> existsExpr) {
  ExistsExpr *expr = static_cast<ExistsExpr *>(existsExpr.get());
  std::vector<const Array *> boundVariables = expr->variables;
  ref<Expr> body = expr->body;
  std::vector<ref<Expr> > interpolantPack;
  std::vector<ref<Expr> > equalityPack;

  // We only simplify a conjunction of interpolant and equalities
  if (!llvm::isa<AndExpr>(body))
    return existsExpr;

  // If the post-simplified body was a constant, simply return the body;
  if (llvm::isa<ConstantExpr>(body))
    return body;

  // The equality constraint is only a single disjunctive clause
  // of a CNF formula. In this case we simplify nothing.
  if (llvm::isa<OrExpr>(body->getKid(1)))
    return existsExpr;
  equalityPack.clear();
  ref<Expr> fullEqualityConstraint =
      simplifyEqualityExpr(equalityPack, body->getKid(1));

  interpolantPack.clear();
  ref<Expr> simplifiedInterpolant =
      simplifyInterpolantExpr(interpolantPack, body->getKid(0));
  if (llvm::isa<ConstantExpr>(simplifiedInterpolant))
    return fullEqualityConstraint;

  std::vector<InequalityExpr *> inequalityPack;

  // STEP 1a: represent Klee expression in equality pack
  // into InequalityExpr data structure that enable us to do arithmetic
  // operation
  for (std::vector<ref<Expr> >::iterator it = equalityPack.begin(),
                                         itEnd = equalityPack.end();
       it != itEnd; ++it) {

    ref<Expr> currExpr = *it;
    std::map<ref<Expr>, int64_t> left = getCoefficient(currExpr->getKid(0));
    std::map<ref<Expr>, int64_t> right = getCoefficient(currExpr->getKid(1));

    InequalityExpr *ineq1 =
        new InequalityExpr(left, right, Expr::Sle, currExpr);
    InequalityExpr *ineq2 =
        new InequalityExpr(left, right, Expr::Sge, currExpr);

    inequalityPack.push_back(ineq1);
    inequalityPack.push_back(ineq2);
  }

  // STEP 1b: represent Klee expression in interpolant pack
  // into InequalityExpr data structure that enable us to do arithmetic
  // operation
  for (std::vector<ref<Expr> >::iterator it = interpolantPack.begin(),
                                         itEnd = interpolantPack.end();
       it != itEnd; ++it) {

    ref<Expr> currExpr = *it;
    std::map<ref<Expr>, int64_t> left = getCoefficient(currExpr->getKid(0));
    std::map<ref<Expr>, int64_t> right = getCoefficient(currExpr->getKid(1));
    InequalityExpr *ineq =
        new InequalityExpr(left, right, currExpr->getKind(), currExpr);
    inequalityPack.push_back(ineq);
  }

  // STEP 2: core of fourier-montzkin algorithm
  for (std::vector<const Array *>::iterator xit = boundVariables.begin(),
                                            xitEnd = boundVariables.end();
       xit != xitEnd; ++xit) {
    const Array *currExistVar = *xit;

    std::vector<InequalityExpr *> lessThanPack;
    std::vector<InequalityExpr *> greaterThanPack;
    std::vector<InequalityExpr *> nonePack;

    // STEP 2a: normalized the inequality expression into the
    // form such that exist variables are located on the left hand side
    for (std::vector<InequalityExpr *>::iterator inqt = inequalityPack.begin(),
                                                 inqtEnd = inequalityPack.end();
         inqt != inqtEnd; ++inqt) {

      InequalityExpr *currIneq = *inqt;
      std::map<ref<Expr>, int64_t> newLeft = currIneq->getLeft();
      std::map<ref<Expr>, int64_t> newRight = currIneq->getRight();

      bool isOnFocusVarOnLeft = false;
      normalization(currExistVar, currIneq->getKind(), newLeft, newRight,
                    isOnFocusVarOnLeft);

      if (newLeft.size() > 0 && newRight.size() > 0) {
        currIneq->updateLeft(newLeft);
        currIneq->updateRight(newRight);
      }

      // STEP 2b: divide the inequalityPack into 3 separated pack
      // based on its operator existVar <= (lessThanPack), existVar >=
      // (greaterThanPack),
      // or (nonePack) if there's no on focus exist variable in the equation.
      classification(currExistVar, currIneq, lessThanPack, greaterThanPack,
                     nonePack, isOnFocusVarOnLeft);
    }

    // STEP 3: matching between inequality
    std::vector<InequalityExpr *> resultPack;
    resultPack = matching(lessThanPack, greaterThanPack);
    inequalityPack.clear();
    inequalityPack.insert(inequalityPack.end(), resultPack.begin(),
                          resultPack.end());
    inequalityPack.insert(inequalityPack.end(), nonePack.begin(),
                          nonePack.end());
  }

  // STEP 4: reconstruct the result back to Klee Expr
  if (inequalityPack.size() == 0)
    return existsExpr;

  ref<Expr> result = reconstructExpr(inequalityPack);
  return result;
}

ref<Expr> SubsumptionTableEntry::reconstructExpr(
    std::vector<InequalityExpr *> inequalityPack) {
  ref<Expr> result;

  for (std::vector<InequalityExpr *>::iterator it1 = inequalityPack.begin(),
                                               it1End = inequalityPack.end();
       it1 != it1End; ++it1) {

    InequalityExpr *currInequality = *it1;
    ref<Expr> temp;
    ref<Expr> leftExpr;
    ref<Expr> rightExpr;

    for (std::map<ref<Expr>, int64_t>::iterator
             l = currInequality->getLeft().begin(),
             lEnd = currInequality->getLeft().end();
         l != lEnd; ++l) {

      ref<Expr> tempLeft;

      if (llvm::isa<ConstantExpr>(l->first)) {
        tempLeft = ConstantExpr::create(l->second, l->first->getWidth());
      } else {
        tempLeft = l->first;
        if (l->second > 1) {
          tempLeft = MulExpr::create(
              tempLeft, ConstantExpr::create(l->second, l->first->getWidth()));
        }
      }

      if (leftExpr.get()) {
        leftExpr = AddExpr::create(leftExpr, tempLeft);
      } else {
        leftExpr = tempLeft;
      }
    }

    for (std::map<ref<Expr>, int64_t>::iterator
             r = currInequality->getRight().begin(),
             rEnd = currInequality->getRight().end();
         r != rEnd; ++r) {

      ref<Expr> currExpr = r->first;
      ref<Expr> tempRight;
      if (llvm::isa<ConstantExpr>(r->first)) {
        tempRight = ConstantExpr::create(r->second, r->first->getWidth());
      } else {
        tempRight = r->first;
        if (r->second > 1) {
          tempRight = MulExpr::create(
              tempRight, ConstantExpr::create(r->second, r->first->getWidth()));
        }
      }

      if (rightExpr.get()) {
        rightExpr = AddExpr::create(rightExpr, tempRight);
      } else {
        rightExpr = tempRight;
      }
    }
    temp = createBinaryExpr(currInequality->getKind(), leftExpr, rightExpr);

    if (result.get()) {
      result = AndExpr::alloc(result, temp);
    } else {
      result = temp;
    }
  }
  return result;
}

void SubsumptionTableEntry::classification(
    const Array *onFocusExistential, InequalityExpr *currIneq,
    std::vector<InequalityExpr *> &lessThanPack,
    std::vector<InequalityExpr *> &greaterThanPack,
    std::vector<InequalityExpr *> &nonePack, bool isOnFocusVarOnLeft) {
  if (!isOnFocusVarOnLeft) {
    nonePack.push_back(currIneq);
  } else {
    if (currIneq->getLeft().size() == 1) {
      if (currIneq->getKind() == Expr::Sle)
        lessThanPack.push_back(currIneq);
      else if (currIneq->getKind() == Expr::Sge)
        greaterThanPack.push_back(currIneq);
    } else {
      nonePack.push_back(currIneq);
    }
  }
}
// assume lessThanPack: (shadow_expr <= a) and greaterThanPack: (shadow_expr >=
// b), it would be b <= a
// greaterThanPack.right <= lessThanPack.right
std::vector<InequalityExpr *>
SubsumptionTableEntry::matching(std::vector<InequalityExpr *> lessThanPack,
                                std::vector<InequalityExpr *> greaterThanPack) {
  std::vector<InequalityExpr *> result;

  for (std::vector<InequalityExpr *>::iterator it1 = lessThanPack.begin(),
                                               it1End = lessThanPack.end();
       it1 != it1End; ++it1) {
    InequalityExpr *currLT = *it1;
    std::map<ref<Expr>, int64_t> right = currLT->getRight();

    for (std::vector<InequalityExpr *>::iterator it2 = greaterThanPack.begin(),
                                                 it2End = greaterThanPack.end();
         it2 != it2End; ++it2) {
      InequalityExpr *currGT = *it2;
      std::map<ref<Expr>, int64_t> left = currGT->getRight();
      simplifyMatching(left, right);
      if (left.size() > 0 && right.size() > 0) {
        InequalityExpr *ineq = new InequalityExpr(left, right, Expr::Sle, NULL);
        result.push_back(ineq);
      }
    }
  }
  return result;
}

void
SubsumptionTableEntry::simplifyMatching(std::map<ref<Expr>, int64_t> &left,
                                        std::map<ref<Expr>, int64_t> &right) {
  for (std::map<ref<Expr>, int64_t>::iterator l = left.begin(),
                                              lEnd = left.end();
       l != lEnd; ++l) {

    for (std::map<ref<Expr>, int64_t>::iterator r = right.begin(),
                                                rEnd = right.end();
         r != rEnd; ++r) {

      if (r->first.operator==(l->first)) {
        if (l->second > r->second) {
          l->second = l->second - r->second;
          r->second = 0;
        } else if (l->second < r->second) {
          r->second = r->second - l->second;
          l->second = 0;
        } else if (l->second == r->second) {
          l->second = 0;
          r->second = 0;
        }
      }
    }
  }

  std::map<ref<Expr>, int64_t>::iterator itLeft = left.begin();
  while (itLeft != left.end()) {
    if (itLeft->second == 0)
      left.erase(itLeft++);
    else
      ++itLeft;
  }

  std::map<ref<Expr>, int64_t>::iterator itRight = right.begin();
  while (itRight != right.end()) {
    if (itRight->second == 0)
      right.erase(itRight++);
    else
      ++itRight;
  }
}

void SubsumptionTableEntry::normalization(const Array *onFocusExistential,
                                          Expr::Kind kind,
                                          std::map<ref<Expr>, int64_t> &left,
                                          std::map<ref<Expr>, int64_t> &right,
                                          bool &isOnFocusVarOnLeft) {

  std::map<ref<Expr>, int64_t>::iterator itLeft = left.begin();

  while (itLeft != left.end()) {

    ref<Expr> currExpr = itLeft->first;
    int64_t currCoefficient = itLeft->second;

    if (llvm::isa<ConcatExpr>(currExpr)) {
      currExpr = getReadExprFromConcatExpr(currExpr);
    }

    if (llvm::isa<ReadExpr>(currExpr)) {
      ReadExpr *readExpr = llvm::dyn_cast<ReadExpr>(currExpr.get());
      const Array *array = (readExpr->updates).root;

      // move variable other than on focus existential variable to the right
      // hand side
      // then, delete it from left map
      if (array == onFocusExistential) {
        ++itLeft;
        isOnFocusVarOnLeft = true;
      } else {
        currCoefficient = currCoefficient * -1;

        if (right.count(itLeft->first) > 0) { // contains
          right.at(itLeft->first) = right.at(itLeft->first) + currCoefficient;
        } else {
          right.insert(std::make_pair(itLeft->first, currCoefficient));
        }
        left.erase(itLeft++);
      }
    } else if (llvm::isa<ConstantExpr>(itLeft->first)) {
      currCoefficient = currCoefficient * -1;
      if (right.count(itLeft->first) > 0) {
        right.at(itLeft->first) = right.at(itLeft->first) + currCoefficient;
      } else {
        right.insert(std::make_pair(itLeft->first, currCoefficient));
      }
      left.erase(itLeft++);
    } else {
      ++itLeft;
    }
  }

  // if we find on focus exist variable on the right hand side,
  // move it to the left hand side
  std::map<ref<Expr>, int64_t>::iterator itRight = right.begin();
  while (itRight != right.end()) {

    ref<Expr> currExpr = itRight->first;
    int64_t currCoefficient = itRight->second;

    if (llvm::isa<ConcatExpr>(currExpr)) {
      currExpr = getReadExprFromConcatExpr(currExpr);
    }

    if (llvm::isa<ReadExpr>(currExpr)) {
      ReadExpr *readExpr = llvm::dyn_cast<ReadExpr>(currExpr.get());
      const Array *array = (readExpr->updates).root;

      if (array == onFocusExistential) {
        currCoefficient = currCoefficient * -1;
        if (left.count(itRight->first) > 0) {
          left.at(itRight->first) = left.at(itRight->first) + currCoefficient;
        } else {
          left.insert(std::make_pair(itRight->first, currCoefficient));
        }
        right.erase(itRight++);
        isOnFocusVarOnLeft = true;
      } else {
        ++itRight;
      }
    } else {
      ++itRight;
    }
  }
}

std::map<ref<Expr>, int64_t>
SubsumptionTableEntry::getCoefficient(ref<Expr> expr) {
  std::map<ref<Expr>, int64_t> map;
  if (expr->getNumKids() == 2 && !llvm::isa<ConcatExpr>(expr)) {
    return coefficientOperation(expr->getKind(),
                                getCoefficient(expr->getKid(0)),
                                getCoefficient(expr->getKid(1)));
  }

  if (expr->getNumKids() < 2 || llvm::isa<ConcatExpr>(expr)) {
    if (llvm::isa<ConstantExpr>(expr))
      map.insert(std::make_pair(ConstantExpr::alloc(0, expr->getWidth()),
                                llvm::dyn_cast<ConstantExpr>(expr.get())
                                    ->getAPValue()
                                    .getSExtValue()));
    else {
      int64_t count = 1;
      map.insert(std::make_pair(expr, count));
    }
  }

  return map;
}

ref<Expr> SubsumptionTableEntry::getReadExprFromConcatExpr(ref<Expr> expr) {
  if (llvm::isa<ReadExpr>(expr))
    return expr;

  return getReadExprFromConcatExpr(expr->getKid(1));
}

std::map<ref<Expr>, int64_t>
SubsumptionTableEntry::coefficientOperation(Expr::Kind kind,
                                            std::map<ref<Expr>, int64_t> map1,
                                            std::map<ref<Expr>, int64_t> map2) {

  for (std::map<ref<Expr>, int64_t>::iterator it1 = map1.begin(),
                                              it1End = map1.end();
       it1 != it1End; ++it1) {

    ref<Expr> expr1 = it1->first;
    bool isFound = false;
    for (std::map<ref<Expr>, int64_t>::iterator it2 = map2.begin(),
                                                it2End = map2.end();
         it2 != it2End; ++it2) {

      ref<Expr> expr2 = it2->first;
      if (expr1.operator==(expr2)) {
        if (kind == Expr::Add) {
          it2->second = it1->second + it2->second;
        } else if (kind == Expr::Sub) {
          it2->second = it1->second - it2->second;
        }
        isFound = true;
        break;
      }
    }

    if (!isFound) {
      map2.insert(std::make_pair(it1->first, it1->second));
    }
  }

  return map2;
}

void
SubsumptionTableEntry::normalizeExpr(std::vector<ref<Expr> > equalityPack,
                                     std::vector<ref<Expr> > &inequalityPack) {
  for (std::vector<ref<Expr> >::iterator it = equalityPack.begin(),
                                         itEnd = equalityPack.end();
       it != itEnd; ++it) {

    ref<Expr> equalityConstraint = *it;

    inequalityPack.push_back(createBinaryExpr(Expr::Sle,
                                              equalityConstraint->getKid(0),
                                              equalityConstraint->getKid(1)));
    inequalityPack.push_back(createBinaryExpr(Expr::Sle,
                                              equalityConstraint->getKid(1),
                                              equalityConstraint->getKid(0)));
  }
}

ref<Expr> SubsumptionTableEntry::createBinaryExpr(Expr::Kind kind,
                                                  ref<Expr> lhs,
                                                  ref<Expr> rhs) {
  std::vector<Expr::CreateArg> exprs;
  Expr::CreateArg arg1(lhs);
  Expr::CreateArg arg2(rhs);
  exprs.push_back(arg1);
  exprs.push_back(arg2);
  return Expr::createFromKind(kind, exprs);
}

ref<Expr> SubsumptionTableEntry::simplifyArithmeticBody(ref<Expr> existsExpr) {
  assert(llvm::isa<ExistsExpr>(existsExpr));

  std::vector<ref<Expr> > interpolantPack;
  std::vector<ref<Expr> > equalityPack;

  ExistsExpr *expr = static_cast<ExistsExpr *>(existsExpr.get());

  std::vector<const Array *> boundVariables = expr->variables;
  // We assume that the body is always a conjunction of interpolant in terms of
  // shadow (existentially-quantified) variables and state equality constraints,
  // which may contain both normal and shadow variables.
  ref<Expr> body = expr->body;

  // We only simplify a conjunction of interpolant and equalities
  if (!llvm::isa<AndExpr>(body))
    return existsExpr;

  // If the post-simplified body was a constant, simply return the body;
  if (llvm::isa<ConstantExpr>(body))
    return body;

  // The equality constraint is only a single disjunctive clause
  // of a CNF formula. In this case we simplify nothing.
  if (llvm::isa<OrExpr>(body->getKid(1)))
    return existsExpr;

  // Here we process equality constraints of shadow and normal variables.
  // The following procedure returns simplified version of the expression
  // by reducing any equality expression into constant (TRUE/FALSE).
  // if body is A and (Eq 2 4), body can be simplified into false.
  // if body is A and (Eq 2 2), body can be simplified into A.
  //
  // Along the way, It also collects the remaining equalities in equalityPack.
  // The equality constraints (body->getKid(1)) is a CNF of the form
  // c1 /\ ... /\ cn. This procedure collects into equalityPack all ci for
  // 1<=i<=n which are atomic equalities, to be used in simplifying the
  // interpolant.
  // ref<Expr> equalityConstraint =
  ref<Expr> fullEqualityConstraint =
      simplifyEqualityExpr(equalityPack, body->getKid(1));

  // Try to simplify the interpolant. If the resulting simplification
  // was a constant (true), then the equality constraints would contain
  // equality with constants only and no equality with shadow (existential)
  // variables, hence it should be safe to simply return the equality
  // constraint.
  interpolantPack.clear();
  ref<Expr> simplifiedInterpolant =
      simplifyInterpolantExpr(interpolantPack, body->getKid(0));
  if (llvm::isa<ConstantExpr>(simplifiedInterpolant))
    return fullEqualityConstraint;

  ref<Expr> newInterpolant;

  for (std::vector<ref<Expr> >::iterator it = interpolantPack.begin(),
                                         itEnd = interpolantPack.end();
       it != itEnd; ++it) {

    ref<Expr> interpolantAtom = (*it); // For example C cmp D

    for (std::vector<ref<Expr> >::iterator itEq = equalityPack.begin(),
                                           itEqEnd = equalityPack.end();
         itEq != itEqEnd; ++itEq) {

      ref<Expr> equalityConstraint =
          *itEq; // For example, say this constraint is A == B
      if (equalityConstraint->isFalse()) {
        return ConstantExpr::alloc(0, Expr::Bool);
      } else if (equalityConstraint->isTrue()) {
        return ConstantExpr::alloc(1, Expr::Bool);
      }
      // Left-hand side of the equality formula (A in our example) that contains
      // the shadow expression (we assume that the existentially-quantified
      // shadow variable is always on the left side).
      ref<Expr> equalityConstraintLeft = equalityConstraint->getKid(0);

      // Right-hand side of the equality formula (B in our example) that does
      // not contain existentially-quantified shadow variables.
      ref<Expr> equalityConstraintRight = equalityConstraint->getKid(1);

      ref<Expr> newIntpLeft;
      ref<Expr> newIntpRight;

      // When the if condition holds, we perform substitution
      if (containShadowExpr(equalityConstraintLeft,
                            interpolantAtom->getKid(0))) {
        // Here we perform substitution, where given
        // an interpolant atom and an equality constraint,
        // we try to find a subexpression in the equality constraint
        // that matches the lhs expression of the interpolant atom.

        // Here we assume that the equality constraint is A == B and the
        // interpolant atom is C cmp D.

        // newIntpLeft == B
        newIntpLeft = equalityConstraintRight;

        // If equalityConstraintLeft does not have any arithmetic operation
        // we could directly assign newIntpRight = D, otherwise,
        // newIntpRight == A[D/C]
        if (!llvm::isa<BinaryExpr>(equalityConstraintLeft))
          newIntpRight = interpolantAtom->getKid(1);
        else {
          // newIntpRight is A, but with every occurrence of C replaced with D
          // i.e., newIntpRight == A[D/C]
          newIntpRight =
              replaceExpr(equalityConstraintLeft, interpolantAtom->getKid(0),
                          interpolantAtom->getKid(1));
        }

        interpolantAtom =
            createBinaryOfSameKind(interpolantAtom, newIntpLeft, newIntpRight);
      }
    }

    // We add the modified interpolant conjunct into a conjunction of
    // new interpolants.
    if (newInterpolant.get()) {
      newInterpolant = AndExpr::alloc(newInterpolant, interpolantAtom);
    } else {
      newInterpolant = interpolantAtom;
    }
  }

  ref<Expr> newBody;

  if (newInterpolant.get()) {
    if (!hasExistentials(expr->variables, newInterpolant))
      return newInterpolant;

    newBody = AndExpr::alloc(newInterpolant, fullEqualityConstraint);
  } else {
    newBody = AndExpr::alloc(simplifiedInterpolant, fullEqualityConstraint);
  }

  return simplifyWithFourierMotzkin(existsExpr->rebuild(&newBody));
}

ref<Expr> SubsumptionTableEntry::replaceExpr(ref<Expr> originalExpr,
                                             ref<Expr> replacedExpr,
                                             ref<Expr> substituteExpr) {
  // We only handle binary expressions
  if (!llvm::isa<BinaryExpr>(originalExpr) ||
      llvm::isa<ConcatExpr>(originalExpr))
    return originalExpr;

  if (originalExpr->getKid(0) == replacedExpr)
    return createBinaryOfSameKind(originalExpr, substituteExpr,
                                  originalExpr->getKid(1));

  if (originalExpr->getKid(1) == replacedExpr)
    return createBinaryOfSameKind(originalExpr, originalExpr->getKid(0),
                                  substituteExpr);

  return createBinaryOfSameKind(
      originalExpr,
      replaceExpr(originalExpr->getKid(0), replacedExpr, substituteExpr),
      replaceExpr(originalExpr->getKid(1), replacedExpr, substituteExpr));
}

bool SubsumptionTableEntry::containShadowExpr(ref<Expr> expr,
                                              ref<Expr> shadowExpr) {
  if (expr.operator==(shadowExpr))
    return true;
  if (expr->getNumKids() < 2 && expr.operator!=(shadowExpr))
    return false;

  return containShadowExpr(expr->getKid(0), shadowExpr) ||
         containShadowExpr(expr->getKid(1), shadowExpr);
}

ref<Expr> SubsumptionTableEntry::createBinaryOfSameKind(ref<Expr> originalExpr,
                                                        ref<Expr> newLhs,
                                                        ref<Expr> newRhs) {
  std::vector<Expr::CreateArg> exprs;
  Expr::CreateArg arg1(newLhs);
  Expr::CreateArg arg2(newRhs);
  exprs.push_back(arg1);
  exprs.push_back(arg2);
  return Expr::createFromKind(originalExpr->getKind(), exprs);
}

ref<Expr> SubsumptionTableEntry::simplifyInterpolantExpr(
    std::vector<ref<Expr> > &interpolantPack, ref<Expr> expr) {
  if (expr->getNumKids() < 2)
    return expr;

  if (llvm::isa<EqExpr>(expr) && llvm::isa<ConstantExpr>(expr->getKid(0)) &&
      llvm::isa<ConstantExpr>(expr->getKid(1))) {
    return (expr->getKid(0).operator==(expr->getKid(1)))
               ? ConstantExpr::alloc(1, Expr::Bool)
               : ConstantExpr::alloc(0, Expr::Bool);
  } else if (llvm::isa<NeExpr>(expr) &&
             llvm::isa<ConstantExpr>(expr->getKid(0)) &&
             llvm::isa<ConstantExpr>(expr->getKid(1))) {
    return (expr->getKid(0).operator!=(expr->getKid(1)))
               ? ConstantExpr::alloc(1, Expr::Bool)
               : ConstantExpr::alloc(0, Expr::Bool);
  }

  ref<Expr> lhs = expr->getKid(0);
  ref<Expr> rhs = expr->getKid(1);

  if (!llvm::isa<AndExpr>(expr)) {

    // If the current expression has a form like (Eq false P), where P is some
    // comparison, we change it into the negation of P.
    if (llvm::isa<EqExpr>(expr) && expr->getKid(0)->isFalse()) {
      if (llvm::isa<SltExpr>(rhs)) {
        expr = SgeExpr::create(rhs->getKid(0), rhs->getKid(1));
      } else if (llvm::isa<SgeExpr>(rhs)) {
        expr = SltExpr::create(rhs->getKid(0), rhs->getKid(1));
      } else if (llvm::isa<SleExpr>(rhs)) {
        expr = SgtExpr::create(rhs->getKid(0), rhs->getKid(1));
      } else if (llvm::isa<SgtExpr>(rhs)) {
        expr = SleExpr::create(rhs->getKid(0), rhs->getKid(1));
      }
    }

    // Collect unique interpolant expressions in one vector
    if (std::find(interpolantPack.begin(), interpolantPack.end(), expr) ==
        interpolantPack.end())
      interpolantPack.push_back(expr);

    return expr;
  }

  return AndExpr::alloc(simplifyInterpolantExpr(interpolantPack, lhs),
                        simplifyInterpolantExpr(interpolantPack, rhs));
}

ref<Expr> SubsumptionTableEntry::simplifyEqualityExpr(
    std::vector<ref<Expr> > &equalityPack, ref<Expr> expr) {
  if (expr->getNumKids() < 2)
    return expr;

  if (llvm::isa<EqExpr>(expr)) {
    if (llvm::isa<ConstantExpr>(expr->getKid(0)) &&
        llvm::isa<ConstantExpr>(expr->getKid(1))) {
      return (expr->getKid(0).operator==(expr->getKid(1)))
                 ? ConstantExpr::alloc(1, Expr::Bool)
                 : ConstantExpr::alloc(0, Expr::Bool);
    }

    // Collect unique equality and in-equality expressions in one vector
    if (std::find(equalityPack.begin(), equalityPack.end(), expr) ==
        equalityPack.end())
      equalityPack.push_back(expr);

    return expr;
  }

  if (llvm::isa<AndExpr>(expr)) {
    ref<Expr> lhs = simplifyEqualityExpr(equalityPack, expr->getKid(0));
    if (lhs->isFalse())
      return lhs;

    ref<Expr> rhs = simplifyEqualityExpr(equalityPack, expr->getKid(1));
    if (rhs->isFalse())
      return rhs;

    if (lhs->isTrue())
      return rhs;

    if (rhs->isTrue())
      return lhs;

    return AndExpr::alloc(lhs, rhs);
  } else if (llvm::isa<OrExpr>(expr)) {
    // We provide throw-away dummy equalityPack, as we do not use the atomic
    // equalities within disjunctive clause to simplify the interpolant.
    std::vector<ref<Expr> > dummy;
    ref<Expr> lhs = simplifyEqualityExpr(dummy, expr->getKid(0));
    if (lhs->isTrue())
      return lhs;

    ref<Expr> rhs = simplifyEqualityExpr(dummy, expr->getKid(1));
    if (rhs->isTrue())
      return rhs;

    if (lhs->isFalse())
      return rhs;

    if (rhs->isFalse())
      return lhs;

    return OrExpr::alloc(lhs, rhs);
  }

  assert(!"Invalid expression type.");
}

ref<Expr> SubsumptionTableEntry::simplifyExistsExpr(ref<Expr> existsExpr) {
  assert(llvm::isa<ExistsExpr>(existsExpr));

  ref<Expr> ret = simplifyWithFourierMotzkin(existsExpr);
  if (llvm::isa<ExistsExpr>(ret))
    return ret;

  return ret;
}

bool SubsumptionTableEntry::subsumed(TimingSolver *solver,
                                     ExecutionState &state, double timeout) {
  // Check if we are at the right program point
  if (state.itreeNode == 0 || reinterpret_cast<uintptr_t>(state.pc->inst) !=
                                  state.itreeNode->getNodeId() ||
      state.itreeNode->getNodeId() != nodeId)
    return false;

  // Quick check for subsumption in case the interpolant is empty
  if (empty())
    return true;

  std::map<llvm::Value *, ref<Expr> > stateSingletonStore =
      state.itreeNode->getLatestCoreExpressions();
  std::map<llvm::Value *, std::vector<ref<Expr> > > stateCompositeStore =
      state.itreeNode->getCompositeCoreExpressions();

  ref<Expr> stateEqualityConstraints;
  for (std::vector<llvm::Value *>::iterator
           itBegin = singletonStoreKeys.begin(),
           itEnd = singletonStoreKeys.end(), it = itBegin;
       it != itEnd; ++it) {
    const ref<Expr> lhs = singletonStore[*it];
    const ref<Expr> rhs = stateSingletonStore[*it];
    stateEqualityConstraints =
        (it == itBegin ? EqExpr::alloc(lhs, rhs)
                       : AndExpr::alloc(EqExpr::alloc(lhs, rhs),
                                        stateEqualityConstraints));
  }

  for (std::vector<llvm::Value *>::iterator it = compositeStoreKeys.begin(),
                                            itEnd = compositeStoreKeys.end();
       it != itEnd; ++it) {
    std::vector<ref<Expr> > lhsList = compositeStore[*it];
    std::vector<ref<Expr> > rhsList = stateCompositeStore[*it];

    ref<Expr> auxDisjuncts;
    bool auxDisjunctsEmpty = true;

    for (std::vector<ref<Expr> >::iterator lhsIter = lhsList.begin(),
                                           lhsIterEnd = lhsList.end();
         lhsIter != lhsIterEnd; ++lhsIter) {
      for (std::vector<ref<Expr> >::iterator rhsIter = rhsList.begin(),
                                             rhsIterEnd = rhsList.end();
           rhsIter != rhsIterEnd; ++rhsIter) {
        const ref<Expr> lhs = *lhsIter;
        const ref<Expr> rhs = *rhsIter;

        if (auxDisjunctsEmpty) {
          auxDisjuncts = EqExpr::alloc(lhs, rhs);
          auxDisjunctsEmpty = false;
        } else {
          auxDisjuncts = OrExpr::alloc(EqExpr::alloc(lhs, rhs), auxDisjuncts);
        }
      }
    }

    if (!auxDisjunctsEmpty) {
      stateEqualityConstraints =
          stateEqualityConstraints.get()
              ? AndExpr::alloc(auxDisjuncts, stateEqualityConstraints)
              : auxDisjuncts;
    }
  }

  // We create path condition needed constraints marking structure
  std::map<ref<Expr>, PathConditionMarker *> markerMap =
      state.itreeNode->makeMarkerMap();

  Solver::Validity result;
  ref<Expr> query;

  // Here we build the query, after which it is always a conjunction of
  // the interpolant and the state equality constraints.
  if (interpolant.get()) {
    query =
        stateEqualityConstraints.get()
            ? AndExpr::alloc(interpolant, stateEqualityConstraints)
            : AndExpr::alloc(interpolant, ConstantExpr::create(1, Expr::Bool));
  } else if (stateEqualityConstraints.get()) {
    query = AndExpr::alloc(ConstantExpr::create(1, Expr::Bool),
                           stateEqualityConstraints);
  } else {
    // Here both the interpolant constraints and state equality
    // constraints are empty, therefore everything gets subsumed
    return true;
  }

  if (!existentials.empty()) {
    ref<Expr> existsExpr = ExistsExpr::create(existentials, query);
    // llvm::errs() << "Before simplification:\n";
    // ExprPPrinter::printQuery(llvm::errs(), state.constraints, existsExpr);
    query = simplifyExistsExpr(existsExpr);
  }

  bool success = false;

  Z3Solver *z3solver = 0;

  // We call the solver only when the simplified query is
  // not a constant.
  if (!llvm::isa<ConstantExpr>(query)) {
    //     llvm::errs() << "Querying for subsumption check:\n";
    //     ExprPPrinter::printQuery(llvm::errs(), state.constraints, query);

    if (!existentials.empty() && llvm::isa<ExistsExpr>(query)) {
      // llvm::errs() << "Existentials not empty\n";

      // Instantiate a new Z3 solver to make sure we use Z3
      // without pre-solving optimizations. It would be nice
      // in the future to just run solver->evaluate so that
      // the optimizations can be used, but this requires
      // handling of quantified expressions by KLEE's pre-solving
      // procedure, which does not exist currently.
      z3solver = new Z3Solver();

      z3solver->setCoreSolverTimeout(timeout);

      success = z3solver->directComputeValidity(Query(state.constraints, query),
                                                result);
      z3solver->setCoreSolverTimeout(0);
    } else {
      // llvm::errs() << "No existential\n";

      // We call the solver in the standard way if the
      // formula is unquantified.
      solver->setTimeout(timeout);
      success = solver->evaluate(state, query, result);
      solver->setTimeout(0);
    }
  } else {
    if (query->isTrue())
      return true;
    return false;
  }

  if (success && result == Solver::True) {
    //     llvm::errs() << "Solver decided validity\n";
    std::vector<ref<Expr> > unsatCore;
    if (z3solver) {
      unsatCore = z3solver->getUnsatCore();
      delete z3solver;
    } else {
      unsatCore = solver->getUnsatCore();
    }

    for (std::vector<ref<Expr> >::iterator it1 = unsatCore.begin();
         it1 != unsatCore.end(); it1++) {
      markerMap[*it1]->mayIncludeInInterpolant();
    }

  } else {
    if (result != Solver::False)
      //      llvm::errs() << "Solver could not decide (in-)validity\n";

      if (z3solver)
        delete z3solver;

    return false;
  }

  // State subsumed, we mark needed constraints on the
  // path condition.
  AllocationGraph *g = new AllocationGraph();
  for (std::map<ref<Expr>, PathConditionMarker *>::iterator
           it = markerMap.begin(),
           itEnd = markerMap.end();
       it != itEnd; it++) {
    it->second->includeInInterpolant(g);
  }
  ITreeNode::deleteMarkerMap(markerMap);

  // llvm::errs() << "AllocationGraph\n";
  // g->dump();

  // We mark memory allocations needed for the unsatisfiabilty core
  state.itreeNode->computeInterpolantAllocations(g);

  delete g; // Delete the AllocationGraph object
  return true;
}

void SubsumptionTableEntry::dump() const {
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void SubsumptionTableEntry::print(llvm::raw_ostream &stream) const {
  stream << "------------ Subsumption Table Entry ------------\n";
  stream << "Program point = " << nodeId << "\n";
  stream << "interpolant = ";
  if (interpolant.get())
    interpolant->print(stream);
  else
    stream << "(empty)";
  stream << "\n";

  if (!singletonStore.empty()) {
    stream << "singleton allocations = [";
    for (std::map<llvm::Value *, ref<Expr> >::const_iterator
             itBegin = singletonStore.begin(),
             itEnd = singletonStore.end(), it = itBegin;
         it != itEnd; ++it) {
      if (it != itBegin)
        stream << ",";
      stream << "(";
      it->first->print(stream);
      stream << ",";
      it->second->print(stream);
      stream << ")";
    }
    stream << "]\n";
  }

  if (!compositeStore.empty()) {
    stream << "composite allocations = [";
    for (std::map<llvm::Value *, std::vector<ref<Expr> > >::const_iterator
             it0Begin = compositeStore.begin(),
             it0End = compositeStore.end(), it0 = it0Begin;
         it0 != it0End; ++it0) {
      if (it0 != it0Begin)
        stream << ",";
      stream << "(";
      it0->first->print(stream);
      stream << ",[";
      for (std::vector<ref<Expr> >::const_iterator
               it1Begin = it0->second.begin(),
               it1End = it0->second.end(), it1 = it1Begin;
           it1 != it1End; ++it1) {
        if (it1 != it1Begin)
          stream << ",";
        (*it1)->print(stream);
      }
      stream << "])";
    }
    stream << "]\n";
  }

  if (!existentials.empty()) {
    stream << "existentials = [";
    for (std::vector<const Array *>::const_iterator
             itBegin = existentials.begin(),
             itEnd = existentials.end(), it = itBegin;
         it != itEnd; ++it) {
      if (it != itBegin)
        stream << ", ";
      stream << (*it)->name;
    }
    stream << "]\n";
  }
}

/**/

InequalityExpr::InequalityExpr(std::map<ref<Expr>, int64_t> left,
                               std::map<ref<Expr>, int64_t> right,
                               Expr::Kind kind, ref<Expr> originalExpr)
    : left(left), right(right), kind(kind), originalExpr(originalExpr) {}

InequalityExpr::~InequalityExpr() {}

std::map<ref<Expr>, int64_t> InequalityExpr::getLeft() { return left; }

std::map<ref<Expr>, int64_t> InequalityExpr::getRight() { return right; }

Expr::Kind InequalityExpr::getKind() { return kind; }

ref<Expr> InequalityExpr::getOriginalExpr() { return originalExpr; }

void InequalityExpr::updateLeft(std::map<ref<Expr>, int64_t> newLeft) {
  left = newLeft;
}

void InequalityExpr::updateRight(std::map<ref<Expr>, int64_t> newRight) {
  right = newRight;
}

/**/

ITree::ITree(ExecutionState *_root) {
  currentINode = 0;
  if (!_root->itreeNode) {
    currentINode = new ITreeNode(0);
  }
  root = currentINode;
}

ITree::~ITree() {}

bool ITree::checkCurrentStateSubsumption(TimingSolver *solver,
                                         ExecutionState &state,
                                         double timeout) {
  assert(state.itreeNode == currentINode);

  for (std::vector<SubsumptionTableEntry>::iterator it =
           subsumptionTable.begin();
       it != subsumptionTable.end(); it++) {

    if (it->subsumed(solver, state, timeout)) {

      // We mark as subsumed such that the node will not be
      // stored into table (the table already contains a more
      // general entry).
      currentINode->isSubsumed = true;

      return true;
    }
  }
  return false;
}

std::vector<SubsumptionTableEntry> ITree::getStore() {
  return subsumptionTable;
}

void ITree::store(SubsumptionTableEntry subItem) {
  subsumptionTable.push_back(subItem);
}

void ITree::setCurrentINode(ITreeNode *node) { currentINode = node; }

void ITree::remove(ITreeNode *node) {
  assert(!node->left && !node->right);
  do {
    ITreeNode *p = node->parent;

    // As the node is about to be deleted, it must have been completely
    // traversed, hence the correct time to table the interpolant.
    if (!node->isSubsumed) {
      SubsumptionTableEntry entry(node);
      store(entry);
    }

    delete node;
    if (p) {
      if (node == p->left) {
        p->left = 0;
      } else {
        assert(node == p->right);
        p->right = 0;
      }
    }
    node = p;
  } while (node && !node->left && !node->right);
}

std::pair<ITreeNode *, ITreeNode *>
ITree::split(ITreeNode *parent, ExecutionState *left, ExecutionState *right) {
  parent->split(left, right);
  return std::pair<ITreeNode *, ITreeNode *>(parent->left, parent->right);
}

void ITree::markPathCondition(ExecutionState &state, TimingSolver *solver) {
  std::vector<ref<Expr> > unsatCore = solver->getUnsatCore();

  AllocationGraph *g = new AllocationGraph();

  llvm::BranchInst *binst =
      llvm::dyn_cast<llvm::BranchInst>(state.prevPC->inst);
  if (binst) {
    currentINode->dependency->markAllValues(g, binst->getCondition());
  }

  PathCondition *pc = currentINode->pathCondition;

  if (pc != 0) {
    for (std::vector<ref<Expr> >::reverse_iterator it = unsatCore.rbegin();
         it != unsatCore.rend(); it++) {
      while (pc != 0) {
        if (pc->car().compare(it->get()) == 0) {
          pc->includeInInterpolant(g);
          pc = pc->cdr();
          break;
        }
        pc = pc->cdr();
      }
      if (pc == 0)
        break;
    }
  }

  // llvm::errs() << "AllocationGraph\n";
  // g->dump();

  // Compute memory allocations needed by the unsatisfiability core
  currentINode->computeInterpolantAllocations(g);

  delete g; // Delete the AllocationGraph object
}

void ITree::executeAbstractBinaryDependency(llvm::Instruction *instr,
                                            ref<Expr> valueExpr,
                                            ref<Expr> tExpr, ref<Expr> fExpr) {
  currentINode->executeBinaryDependency(instr, valueExpr, tExpr, fExpr);
}

void ITree::executeAbstractMemoryDependency(llvm::Instruction *instr,
                                            ref<Expr> value,
                                            ref<Expr> address) {
  currentINode->executeAbstractMemoryDependency(instr, value, address);
}

void ITree::executeAbstractDependency(llvm::Instruction *instr,
                                      ref<Expr> value) {
  currentINode->executeAbstractDependency(instr, value);
}

void ITree::printNode(llvm::raw_ostream &stream, ITreeNode *n,
                      std::string edges) {
  if (n->left != 0) {
    stream << "\n";
    stream << edges << "+-- L:" << n->left->nodeId;
    if (this->currentINode == n->left) {
      stream << " (active)";
    }
    if (n->right != 0) {
      printNode(stream, n->left, edges + "|   ");
    } else {
      printNode(stream, n->left, edges + "    ");
    }
  }
  if (n->right != 0) {
    stream << "\n";
    stream << edges << "+-- R:" << n->right->nodeId;
    if (this->currentINode == n->right) {
      stream << " (active)";
    }
    printNode(stream, n->right, edges + "    ");
  }
}

void ITree::print(llvm::raw_ostream &stream) {
  stream << "------------------------- ITree Structure "
            "---------------------------\n";
  stream << this->root->nodeId;
  if (this->root == this->currentINode) {
    stream << " (active)";
  }
  this->printNode(stream, this->root, "");
  stream << "\n------------------------- Subsumption Table "
            "-------------------------\n";
  for (std::vector<SubsumptionTableEntry>::iterator
           it = subsumptionTable.begin(),
           itEnd = subsumptionTable.end();
       it != itEnd; ++it) {
    (*it).print(stream);
  }
}

void ITree::dump() { this->print(llvm::errs()); }

/**/

ITreeNode::ITreeNode(ITreeNode *_parent)
    : parent(_parent), left(0), right(0), nodeId(0), isSubsumed(false) {

  pathCondition = (_parent != 0) ? _parent->pathCondition : 0;

  // Inherit the abstract dependency or NULL
  dependency = new Dependency(_parent ? _parent->dependency : 0);
}

ITreeNode::~ITreeNode() {
  // Only delete the path condition if it's not
  // also the parent's path condition
  PathCondition *itEnd = parent ? parent->pathCondition : 0;

  PathCondition *it = pathCondition;
  while (it != itEnd) {
    PathCondition *tmp = it;
    it = it->cdr();
    delete tmp;
  }

  if (dependency)
    delete dependency;
}

uintptr_t ITreeNode::getNodeId() { return nodeId; }

ref<Expr>
ITreeNode::getInterpolant(std::vector<const Array *> &replacements) const {
  return this->pathCondition->packInterpolant(replacements);
}

void ITreeNode::setNodeLocation(uintptr_t programPoint) {
  if (!nodeId)
    nodeId = programPoint;
}

void ITreeNode::addConstraint(ref<Expr> &constraint, llvm::Value *condition) {
  pathCondition =
      new PathCondition(constraint, dependency, condition, pathCondition);
}

void ITreeNode::split(ExecutionState *leftData, ExecutionState *rightData) {
  assert(left == 0 && right == 0);
  leftData->itreeNode = left = new ITreeNode(this);
  rightData->itreeNode = right = new ITreeNode(this);
}

std::map<ref<Expr>, PathConditionMarker *> ITreeNode::makeMarkerMap() const {
  std::map<ref<Expr>, PathConditionMarker *> result;
  for (PathCondition *it = pathCondition; it != 0; it = it->cdr()) {
    result.insert(std::pair<ref<Expr>, PathConditionMarker *>(
        it->car(), new PathConditionMarker(it)));
  }
  return result;
}

void ITreeNode::deleteMarkerMap(
    std::map<ref<Expr>, PathConditionMarker *> &markerMap) {
  for (std::map<ref<Expr>, PathConditionMarker *>::iterator
           it = markerMap.begin(),
           itEnd = markerMap.end();
       it != itEnd; ++it) {
    delete it->second;
  }
  markerMap.clear();
}

void ITreeNode::executeBinaryDependency(llvm::Instruction *i,
                                        ref<Expr> valueExpr, ref<Expr> tExpr,
                                        ref<Expr> fExpr) {
  dependency->executeBinary(i, valueExpr, tExpr, fExpr);
}

void ITreeNode::executeAbstractMemoryDependency(llvm::Instruction *instr,
                                                ref<Expr> value,
                                                ref<Expr> address) {
  dependency->executeMemoryOperation(instr, value, address);
}

void ITreeNode::executeAbstractDependency(llvm::Instruction *instr,
                                          ref<Expr> value) {
  dependency->execute(instr, value);
}

void ITreeNode::bindCallArguments(llvm::Instruction *site,
                                  std::vector<ref<Expr> > &arguments) {
  dependency->bindCallArguments(site, arguments);
}

void ITreeNode::popAbstractDependencyFrame(llvm::CallInst *site,
                                           llvm::Instruction *inst,
                                           ref<Expr> returnValue) {
  // TODO: This is probably where we should simplify
  // the dependency graph by removing callee values.

  dependency->bindReturnValue(site, inst, returnValue);
}

std::map<llvm::Value *, ref<Expr> >
ITreeNode::getLatestCoreExpressions() const {
  std::map<llvm::Value *, ref<Expr> > ret;
  std::vector<const Array *> dummyReplacements;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret =
        parent->dependency->getLatestCoreExpressions(dummyReplacements, false);
  return ret;
}

std::map<llvm::Value *, std::vector<ref<Expr> > >
ITreeNode::getCompositeCoreExpressions() const {
  std::map<llvm::Value *, std::vector<ref<Expr> > > ret;
  std::vector<const Array *> dummyReplacements;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getCompositeCoreExpressions(dummyReplacements,
                                                          false);
  return ret;
}

std::map<llvm::Value *, ref<Expr> >
ITreeNode::getLatestInterpolantCoreExpressions(
    std::vector<const Array *> &replacements) const {
  std::map<llvm::Value *, ref<Expr> > ret;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getLatestCoreExpressions(replacements, true);
  return ret;
}

std::map<llvm::Value *, std::vector<ref<Expr> > >
ITreeNode::getCompositeInterpolantCoreExpressions(
    std::vector<const Array *> &replacements) const {
  std::map<llvm::Value *, std::vector<ref<Expr> > > ret;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getCompositeCoreExpressions(replacements, true);
  return ret;
}

void ITreeNode::computeInterpolantAllocations(AllocationGraph *g) {
  dependency->computeInterpolantAllocations(g);
}

void ITreeNode::dump() const {
  llvm::errs() << "------------------------- ITree Node "
                  "--------------------------------\n";
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void ITreeNode::print(llvm::raw_ostream &stream) const {
  this->print(stream, 0);
}

void ITreeNode::print(llvm::raw_ostream &stream, const unsigned tabNum) const {
  std::string tabs = makeTabs(tabNum);
  std::string tabs_next = appendTab(tabs);

  stream << tabs << "ITreeNode\n";
  stream << tabs_next << "node Id = " << nodeId << "\n";
  stream << tabs_next << "pathCondition = ";
  if (pathCondition == 0) {
    stream << "NULL";
  } else {
    pathCondition->print(stream);
  }
  stream << "\n";
  stream << tabs_next << "Left:\n";
  if (!left) {
    stream << tabs_next << "NULL\n";
  } else {
    left->print(stream, tabNum + 1);
    stream << "\n";
  }
  stream << tabs_next << "Right:\n";
  if (!right) {
    stream << tabs_next << "NULL\n";
  } else {
    right->print(stream, tabNum + 1);
    stream << "\n";
  }
  if (dependency) {
    stream << tabs_next << "------- Abstract Dependencies ----------\n";
    dependency->print(stream, tabNum + 1);
  }
}
