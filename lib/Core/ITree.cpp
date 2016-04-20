//===-- ITree.cpp - Interpolation tree --------------------------*- C++ -*-===//
//
//               The Tracer-X KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementations of the classes for
/// interpolation and subsumption checks for search-space reduction.
///
//===----------------------------------------------------------------------===//

#include "ITree.h"
#include "Dependency.h"
#include "TimingSolver.h"

#include <klee/CommandLine.h>
#include <klee/Expr.h>
#include <klee/Solver.h>
#include <klee/util/ExprPPrinter.h>
#include <fstream>
#include <vector>

using namespace klee;

/**/

std::string SearchTree::PrettyExpressionBuilder::bvConst32(uint32_t value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}
std::string SearchTree::PrettyExpressionBuilder::bvConst64(uint64_t value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}
std::string SearchTree::PrettyExpressionBuilder::bvZExtConst(uint64_t value) {
  return bvConst64(value);
}
std::string SearchTree::PrettyExpressionBuilder::bvSExtConst(uint64_t value) {
  return bvConst64(value);
}
std::string SearchTree::PrettyExpressionBuilder::bvBoolExtract(std::string expr,
                                                               int bit) {
  std::ostringstream stream;
  stream << expr << "[" << bit << "]";
  return stream.str();
}
std::string SearchTree::PrettyExpressionBuilder::bvExtract(std::string expr,
                                                           unsigned top,
                                                           unsigned bottom) {
  std::ostringstream stream;
  stream << expr << "[" << top << "," << bottom << "]";
  return stream.str();
}
std::string SearchTree::PrettyExpressionBuilder::eqExpr(std::string a,
                                                        std::string b) {
  if (a == "false")
    return "!" + b;
  return "(" + a + " = " + b + ")";
}

// logical left and right shift (not arithmetic)
std::string SearchTree::PrettyExpressionBuilder::bvLeftShift(std::string expr,
                                                             unsigned shift) {
  std::ostringstream stream;
  stream << "(" << expr << " \\<\\< " << shift << ")";
  return stream.str();
}
std::string SearchTree::PrettyExpressionBuilder::bvRightShift(std::string expr,
                                                              unsigned shift) {
  std::ostringstream stream;
  stream << "(" << expr << " \\>\\> " << shift << ")";
  return stream.str();
}
std::string
SearchTree::PrettyExpressionBuilder::bvVarLeftShift(std::string expr,
                                                    std::string shift) {
  return "(" + expr + " \\<\\< " + shift + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvVarRightShift(std::string expr,
                                                     std::string shift) {
  return "(" + expr + " \\>\\> " + shift + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvVarArithRightShift(std::string expr,
                                                          std::string shift) {
  return bvVarRightShift(expr, shift);
}

// Some STP-style bitvector arithmetic
std::string
SearchTree::PrettyExpressionBuilder::bvMinusExpr(std::string minuend,
                                                 std::string subtrahend) {
  return "(" + minuend + " - " + subtrahend + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvPlusExpr(std::string augend,
                                                std::string addend) {
  return "(" + augend + " + " + addend + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvMultExpr(std::string multiplacand,
                                                std::string multiplier) {
  return "(" + multiplacand + " * " + multiplier + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvDivExpr(std::string dividend,
                                               std::string divisor) {
  return "(" + dividend + " / " + divisor + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::sbvDivExpr(std::string dividend,
                                                std::string divisor) {
  return "(" + dividend + " / " + divisor + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::bvModExpr(std::string dividend,
                                               std::string divisor) {
  return "(" + dividend + " % " + divisor + ")";
}
std::string
SearchTree::PrettyExpressionBuilder::sbvModExpr(std::string dividend,
                                                std::string divisor) {
  return "(" + dividend + " % " + divisor + ")";
}
std::string SearchTree::PrettyExpressionBuilder::notExpr(std::string expr) {
  return "!(" + expr + ")";
}
std::string SearchTree::PrettyExpressionBuilder::bvAndExpr(std::string lhs,
                                                           std::string rhs) {
  return "(" + lhs + " & " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::bvOrExpr(std::string lhs,
                                                          std::string rhs) {
  return "(" + lhs + " | " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::iffExpr(std::string lhs,
                                                         std::string rhs) {
  return "(" + lhs + " \\<=\\> " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::bvXorExpr(std::string lhs,
                                                           std::string rhs) {
  return "(" + lhs + " xor " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::bvSignExtend(std::string src) {
  return src;
}

// Some STP-style array domain interface
std::string SearchTree::PrettyExpressionBuilder::writeExpr(std::string array,
                                                           std::string index,
                                                           std::string value) {
  return "update(" + array + "," + index + "," + value + ")";
}
std::string SearchTree::PrettyExpressionBuilder::readExpr(std::string array,
                                                          std::string index) {
  return array + "[" + index + "]";
}

// ITE-expression constructor
std::string SearchTree::PrettyExpressionBuilder::iteExpr(
    std::string condition, std::string whenTrue, std::string whenFalse) {
  return "ite(" + condition + "," + whenTrue + "," + whenFalse + ")";
}

// Bitvector comparison
std::string SearchTree::PrettyExpressionBuilder::bvLtExpr(std::string lhs,
                                                          std::string rhs) {
  return "(" + lhs + " \\< " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::bvLeExpr(std::string lhs,
                                                          std::string rhs) {
  return "(" + lhs + " \\<= " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::sbvLtExpr(std::string lhs,
                                                           std::string rhs) {
  return "(" + lhs + " \\< " + rhs + ")";
}
std::string SearchTree::PrettyExpressionBuilder::sbvLeExpr(std::string lhs,
                                                           std::string rhs) {
  return "(" + lhs + " \\<= " + rhs + ")";
}

std::string SearchTree::PrettyExpressionBuilder::constructAShrByConstant(
    std::string expr, unsigned shift, std::string isSigned) {
  return bvRightShift(expr, shift);
}
std::string
SearchTree::PrettyExpressionBuilder::constructMulByConstant(std::string expr,
                                                            uint64_t x) {
  std::ostringstream stream;
  stream << "(" << expr << " * " << x << ")";
  return stream.str();
}
std::string
SearchTree::PrettyExpressionBuilder::constructUDivByConstant(std::string expr_n,
                                                             uint64_t d) {
  std::ostringstream stream;
  stream << "(" << expr_n << " / " << d << ")";
  return stream.str();
}
std::string
SearchTree::PrettyExpressionBuilder::constructSDivByConstant(std::string expr_n,
                                                             uint64_t d) {
  std::ostringstream stream;
  stream << "(" << expr_n << " / " << d << ")";
  return stream.str();
}

std::string
SearchTree::PrettyExpressionBuilder::getInitialArray(const Array *root) {
  std::string array_expr =
      buildArray(root->name.c_str(), root->getDomain(), root->getRange());

  if (root->isConstantArray()) {
    for (unsigned i = 0, e = root->size; i != e; ++i) {
      std::string prev = array_expr;
      array_expr = writeExpr(
          prev, constructActual(ConstantExpr::alloc(i, root->getDomain())),
          constructActual(root->constantValues[i]));
    }
  }
  return array_expr;
}
std::string
SearchTree::PrettyExpressionBuilder::getArrayForUpdate(const Array *root,
                                                       const UpdateNode *un) {
  if (!un) {
    return (getInitialArray(root));
  }
  return writeExpr(getArrayForUpdate(root, un->next),
                   constructActual(un->index), constructActual(un->value));
}

std::string SearchTree::PrettyExpressionBuilder::constructActual(ref<Expr> e) {
  switch (e->getKind()) {
  case Expr::Constant: {
    ConstantExpr *CE = cast<ConstantExpr>(e);
    int width = CE->getWidth();

    // Coerce to bool if necessary.
    if (width == 1)
      return CE->isTrue() ? getTrue() : getFalse();

    // Fast path.
    if (width <= 32)
      return bvConst32(CE->getZExtValue(32));
    if (width <= 64)
      return bvConst64(CE->getZExtValue());

    ref<ConstantExpr> Tmp = CE;
    return bvConst64(Tmp->Extract(0, 64)->getZExtValue());
  }

  // Special
  case Expr::NotOptimized: {
    NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
    return constructActual(noe->src);
  }

  case Expr::Read: {
    ReadExpr *re = cast<ReadExpr>(e);
    assert(re && re->updates.root);
    return readExpr(getArrayForUpdate(re->updates.root, re->updates.head),
                    constructActual(re->index));
  }

  case Expr::Select: {
    SelectExpr *se = cast<SelectExpr>(e);
    std::string cond = constructActual(se->cond);
    std::string tExpr = constructActual(se->trueExpr);
    std::string fExpr = constructActual(se->falseExpr);
    return iteExpr(cond, tExpr, fExpr);
  }

  case Expr::Concat: {
    ConcatExpr *ce = cast<ConcatExpr>(e);
    unsigned numKids = ce->getNumKids();
    std::string res = constructActual(ce->getKid(numKids - 1));
    for (int i = numKids - 2; i >= 0; i--) {
      res = constructActual(ce->getKid(i)) + "." + res;
    }
    return res;
  }

  case Expr::Extract: {
    ExtractExpr *ee = cast<ExtractExpr>(e);
    std::string src = constructActual(ee->expr);
    int width = ee->getWidth();
    if (width == 1) {
      return bvBoolExtract(src, ee->offset);
    } else {
      return bvExtract(src, ee->offset + width - 1, ee->offset);
    }
  }

  // Casting
  case Expr::ZExt: {
    CastExpr *ce = cast<CastExpr>(e);
    std::string src = constructActual(ce->src);
    int width = ce->getWidth();
    if (width == 1) {
      return iteExpr(src, bvOne(), bvZero());
    } else {
      return src;
    }
  }

  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(e);
    std::string src = constructActual(ce->src);
    return bvSignExtend(src);
  }

  // Arithmetic
  case Expr::Add: {
    AddExpr *ae = cast<AddExpr>(e);
    std::string left = constructActual(ae->left);
    std::string right = constructActual(ae->right);
    return bvPlusExpr(left, right);
  }

  case Expr::Sub: {
    SubExpr *se = cast<SubExpr>(e);
    std::string left = constructActual(se->left);
    std::string right = constructActual(se->right);
    return bvMinusExpr(left, right);
  }

  case Expr::Mul: {
    MulExpr *me = cast<MulExpr>(e);
    std::string right = constructActual(me->right);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(me->left))
      if (CE->getWidth() <= 64)
        return constructMulByConstant(right, CE->getZExtValue());

    std::string left = constructActual(me->left);
    return bvMultExpr(left, right);
  }

  case Expr::UDiv: {
    UDivExpr *de = cast<UDivExpr>(e);
    std::string left = constructActual(de->left);

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
      if (CE->getWidth() <= 64) {
        uint64_t divisor = CE->getZExtValue();

        if (bits64::isPowerOfTwo(divisor)) {
          return bvRightShift(left, bits64::indexOfSingleBit(divisor));
        }
      }
    }

    std::string right = constructActual(de->right);
    return bvDivExpr(left, right);
  }

  case Expr::SDiv: {
    SDivExpr *de = cast<SDivExpr>(e);
    std::string left = constructActual(de->left);
    std::string right = constructActual(de->right);
    return sbvDivExpr(left, right);
  }

  case Expr::URem: {
    URemExpr *de = cast<URemExpr>(e);
    std::string left = constructActual(de->left);

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
      if (CE->getWidth() <= 64) {
        uint64_t divisor = CE->getZExtValue();

        if (bits64::isPowerOfTwo(divisor)) {
          unsigned bits = bits64::indexOfSingleBit(divisor);

          // special case for modding by 1 or else we bvExtract -1:0
          if (bits == 0) {
            return bvZero();
          } else {
            return bvExtract(left, bits - 1, 0);
          }
        }
      }
    }

    std::string right = constructActual(de->right);
    return bvModExpr(left, right);
  }

  case Expr::SRem: {
    SRemExpr *de = cast<SRemExpr>(e);
    std::string left = constructActual(de->left);
    std::string right = constructActual(de->right);
    return sbvModExpr(left, right);
  }

  // Bitwise
  case Expr::Not: {
    NotExpr *ne = cast<NotExpr>(e);
    std::string expr = constructActual(ne->expr);
    return notExpr(expr);
  }

  case Expr::And: {
    AndExpr *ae = cast<AndExpr>(e);
    std::string left = constructActual(ae->left);
    std::string right = constructActual(ae->right);
    return bvAndExpr(left, right);
  }

  case Expr::Or: {
    OrExpr *oe = cast<OrExpr>(e);
    std::string left = constructActual(oe->left);
    std::string right = constructActual(oe->right);
    return bvOrExpr(left, right);
  }

  case Expr::Xor: {
    XorExpr *xe = cast<XorExpr>(e);
    std::string left = constructActual(xe->left);
    std::string right = constructActual(xe->right);
    return bvXorExpr(left, right);
  }

  case Expr::Shl: {
    ShlExpr *se = cast<ShlExpr>(e);
    std::string left = constructActual(se->left);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(se->right)) {
      return bvLeftShift(left, (unsigned)CE->getLimitedValue());
    } else {
      std::string amount = constructActual(se->right);
      return bvVarLeftShift(left, amount);
    }
  }

  case Expr::LShr: {
    LShrExpr *lse = cast<LShrExpr>(e);
    std::string left = constructActual(lse->left);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(lse->right)) {
      return bvRightShift(left, (unsigned)CE->getLimitedValue());
    } else {
      std::string amount = constructActual(lse->right);
      return bvVarRightShift(left, amount);
    }
  }

  case Expr::AShr: {
    AShrExpr *ase = cast<AShrExpr>(e);
    std::string left = constructActual(ase->left);
    std::string amount = constructActual(ase->right);
    return bvVarArithRightShift(left, amount);
  }

  // Comparison
  case Expr::Eq: {
    EqExpr *ee = cast<EqExpr>(e);
    std::string left = constructActual(ee->left);
    std::string right = constructActual(ee->right);
    return eqExpr(left, right);
  }

  case Expr::Ult: {
    UltExpr *ue = cast<UltExpr>(e);
    std::string left = constructActual(ue->left);
    std::string right = constructActual(ue->right);
    return bvLtExpr(left, right);
  }

  case Expr::Ule: {
    UleExpr *ue = cast<UleExpr>(e);
    std::string left = constructActual(ue->left);
    std::string right = constructActual(ue->right);
    return bvLeExpr(left, right);
  }

  case Expr::Slt: {
    SltExpr *se = cast<SltExpr>(e);
    std::string left = constructActual(se->left);
    std::string right = constructActual(se->right);
    return sbvLtExpr(left, right);
  }

  case Expr::Sle: {
    SleExpr *se = cast<SleExpr>(e);
    std::string left = constructActual(se->left);
    std::string right = constructActual(se->right);
    return sbvLeExpr(left, right);
  }

  case Expr::Exists: {
    ExistsExpr *xe = cast<ExistsExpr>(e);
    std::string existentials;

    for (std::vector<const Array *>::iterator it = xe->variables.begin(),
                                              itEnd = xe->variables.end();
         it != itEnd; ++it) {
      existentials += (*it)->name;
      if (it != itEnd)
        existentials += ",";
    }

    return "(exists (" + existentials + ") " + constructActual(xe->body) + ")";
  }

  default:
    assert(0 && "unhandled Expr type");
    return getTrue();
  }
}
std::string SearchTree::PrettyExpressionBuilder::construct(ref<Expr> e) {
  PrettyExpressionBuilder *instance = new PrettyExpressionBuilder();
  std::string ret = instance->constructActual(e);
  delete instance;
  return ret;
}

std::string SearchTree::PrettyExpressionBuilder::buildArray(
    const char *name, unsigned indexWidth, unsigned valueWidth) {
  return name;
}

std::string SearchTree::PrettyExpressionBuilder::getTrue() { return "true"; }
std::string SearchTree::PrettyExpressionBuilder::getFalse() { return "false"; }
std::string
SearchTree::PrettyExpressionBuilder::getInitialRead(const Array *root,
                                                    unsigned index) {
  return readExpr(getInitialArray(root), bvConst32(index));
}

SearchTree::PrettyExpressionBuilder::PrettyExpressionBuilder() {}

SearchTree::PrettyExpressionBuilder::~PrettyExpressionBuilder() {}

/**/

unsigned long SearchTree::nextNodeId = 1;

SearchTree *SearchTree::instance = 0;

std::string SearchTree::recurseRender(const SearchTree::Node *node) {
  std::ostringstream stream;

  stream << "Node" << node->nodeId;
  std::string sourceNodeName = stream.str();

  stream << " [shape=record,label=\"{" << node->nodeId << ": " << node->name
         << "\\l";
  for (std::map<PathCondition *, std::pair<std::string, bool> >::const_iterator
           it = node->pathConditionTable.begin(),
           itEnd = node->pathConditionTable.end();
       it != itEnd; ++it) {
    stream << (it->second.first);
    if (it->second.second)
      stream << " ITP";
    stream << "\\l";
  }
  if (node->subsumed) {
    stream << "(subsumed)\\l";
  }
  if (node->falseTarget || node->trueTarget)
    stream << "|{<s0>F|<s1>T}";
  stream << "}\"];\n";

  if (node->falseTarget) {
    stream << sourceNodeName << ":s0 -> Node" << node->falseTarget->nodeId
           << ";\n";
  }
  if (node->trueTarget) {
    stream << sourceNodeName + ":s1 -> Node" << node->trueTarget->nodeId
           << ";\n";
  }
  if (node->falseTarget) {
    stream << recurseRender(node->falseTarget);
  }
  if (node->trueTarget) {
    stream << recurseRender(node->trueTarget);
  }
  return stream.str();
}

std::string SearchTree::render() {
  std::string res("");

  // Simply return empty string when root is undefined
  if (!root)
    return res;

  std::ostringstream stream;
  for (std::map<SearchTree::Node *, SearchTree::Node *>::iterator
           it = subsumptionEdges.begin(),
           itEnd = subsumptionEdges.end();
       it != itEnd; ++it) {
    stream << "Node" << it->first->nodeId << " -> Node" << it->second->nodeId
           << " [style=dashed];\n";
  }

  res = "digraph search_tree {\n";
  res += recurseRender(root);
  res += stream.str();
  res += "}\n";
  return res;
}

SearchTree::SearchTree(ITreeNode *_root) {
  root = SearchTree::Node::createNode(_root->getNodeId());
  itreeNodeMap[_root] = root;
}

SearchTree::~SearchTree() {
  if (root)
    delete root;

  itreeNodeMap.clear();
}

void SearchTree::addChildren(ITreeNode *parent, ITreeNode *falseChild,
                             ITreeNode *trueChild) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  SearchTree::Node *parentNode = instance->itreeNodeMap[parent];
  parentNode->falseTarget =
      SearchTree::Node::createNode(falseChild->getNodeId());
  parentNode->trueTarget = SearchTree::Node::createNode(trueChild->getNodeId());
  instance->itreeNodeMap[falseChild] = parentNode->falseTarget;
  instance->itreeNodeMap[trueChild] = parentNode->trueTarget;
}

void SearchTree::setCurrentNode(ExecutionState &state,
                                const uintptr_t programPoint) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  ITreeNode *iTreeNode = state.itreeNode;
  SearchTree::Node *node = instance->itreeNodeMap[iTreeNode];
  if (!node->nodeId) {
    std::string functionName(
        state.pc->inst->getParent()->getParent()->getName().str());
    node->name = functionName + "\\l";
    llvm::raw_string_ostream out(node->name);
    state.pc->inst->print(out);
    node->name = out.str();

    node->iTreeNodeId = programPoint;
    node->nodeId = nextNodeId++;
  }
}

void SearchTree::markAsSubsumed(ITreeNode *iTreeNode,
                                SubsumptionTableEntry *entry) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  SearchTree::Node *node = instance->itreeNodeMap[iTreeNode];
  node->subsumed = true;
  SearchTree::Node *subsuming = instance->tableEntryMap[entry];
  instance->subsumptionEdges[node] = subsuming;
}

void SearchTree::addPathCondition(ITreeNode *iTreeNode,
                                  PathCondition *pathCondition,
                                  ref<Expr> condition) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  SearchTree::Node *node = instance->itreeNodeMap[iTreeNode];

  std::string s = PrettyExpressionBuilder::construct(condition);

  std::pair<std::string, bool> p(s, false);
  node->pathConditionTable[pathCondition] = p;
  instance->pathConditionMap[pathCondition] = node;
}

void SearchTree::addTableEntryMapping(ITreeNode *iTreeNode,
                                      SubsumptionTableEntry *entry) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  SearchTree::Node *node = instance->itreeNodeMap[iTreeNode];
  instance->tableEntryMap[entry] = node;
}

void SearchTree::setAsCore(PathCondition *pathCondition) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  instance->pathConditionMap[pathCondition]
      ->pathConditionTable[pathCondition]
      .second = true;
}

/// @brief Save the graph
void SearchTree::save(std::string dotFileName) {
  if (!OUTPUT_INTERPOLATION_TREE)
    return;

  assert(SearchTree::instance && "Search tree graph not initialized");

  std::string g(instance->render());
  std::ofstream out(dotFileName.c_str());
  if (!out.fail()) {
    out << g;
    out.close();
  }
}

/**/

PathConditionMarker::PathConditionMarker(PathCondition *pathCondition)
    : maybeCore(false), pathCondition(pathCondition) {}

PathConditionMarker::~PathConditionMarker() {}

void PathConditionMarker::setAsMaybeCore() { maybeCore = true; }

void PathConditionMarker::setAsCore(AllocationGraph *g) {
  if (maybeCore) {
    pathCondition->setAsCore(g);
  }
}

/**/

PathCondition::PathCondition(ref<Expr> &constraint, Dependency *dependency,
                             llvm::Value *condition, PathCondition *prev)
    : constraint(constraint), shadowConstraint(constraint), shadowed(false),
      dependency(dependency),
      condition(dependency ? dependency->getLatestValue(condition, constraint)
                           : 0),
      core(false), tail(prev) {}

PathCondition::~PathCondition() {}

ref<Expr> PathCondition::car() const { return constraint; }

PathCondition *PathCondition::cdr() const { return tail; }

void PathCondition::setAsCore(AllocationGraph *g) {
  // We mark all values to which this constraint depends
  dependency->markAllValues(g, condition);

  // We mark this constraint itself as core
  core = true;

  // We mark constraint as core in the search tree graph as well.
  SearchTree::setAsCore(this);
}

bool PathCondition::isCore() const { return core; }

ref<Expr>
PathCondition::packInterpolant(std::vector<const Array *> &replacements) {
  ref<Expr> res;
  for (PathCondition *it = this; it != 0; it = it->tail) {
    if (it->core) {
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

void PathCondition::dump() const {
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void PathCondition::print(llvm::raw_ostream &stream) const {
  stream << "[";
  for (const PathCondition *it = this; it != 0; it = it->tail) {
    it->constraint->print(stream);
    stream << ": " << (it->core ? "core" : "non-core");
    if (it->tail != 0)
      stream << ",";
  }
  stream << "]";
}

/**/

StatTimer SubsumptionTableEntry::actualSolverCallTimer;

unsigned long SubsumptionTableEntry::checkSolverCount = 0;

unsigned long SubsumptionTableEntry::checkSolverFailureCount = 0;

SubsumptionTableEntry::SubsumptionTableEntry(ITreeNode *node)
    : nodeId(node->getNodeId()) {
  std::vector<const Array *> replacements;

  interpolant = node->getInterpolant(replacements);

  singletonStore = node->getSingletonCoreExpressions(replacements);

  for (std::map<llvm::Value *, ref<Expr> >::iterator
           it = singletonStore.begin(),
           itEnd = singletonStore.end();
       it != itEnd; ++it) {
    singletonStoreKeys.push_back(it->first);
  }

  compositeStore = node->getCompositeCoreExpressions(replacements);
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

bool SubsumptionTableEntry::hasFree(std::vector<const Array *> &existentials,
                                    ref<Expr> expr) {
  for (int i = 0, numKids = expr->getNumKids(); i < numKids; ++i) {
    if (llvm::isa<ReadExpr>(expr)) {
      ReadExpr *readExpr = llvm::dyn_cast<ReadExpr>(expr.get());
      const Array *array = (readExpr->updates).root;
      for (std::vector<const Array *>::iterator it = existentials.begin(),
                                                itEnd = existentials.end();
           it != itEnd; ++it) {
        if ((*it) == array)
          return false;
      }
      return true;
    } else if (hasFree(existentials, expr->getKid(i)))
      return true;
  }
  return false;
}

ref<Expr>
SubsumptionTableEntry::simplifyWithFourierMotzkin(ref<Expr> existsExpr) {
  ExistsExpr *expr = llvm::dyn_cast<ExistsExpr>(existsExpr.get());
  if (!expr)
    return existsExpr;

  llvm::errs() << "Simplifying with Fourier-Motzkin:\n";
  expr->dump();

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

  // STEP 1a: represent KLEE expression in equality pack
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

  // STEP 1b: represent KLEE expression in interpolant pack
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
    std::vector<InequalityExpr *> strictLessThanPack;
    std::vector<InequalityExpr *> strictGreaterThanPack;
    std::vector<InequalityExpr *> nonePack;

    // STEP 2a: normalized the inequality expression into the
    // form such that exist variables are located on the left hand side
    for (std::vector<InequalityExpr *>::iterator inqt = inequalityPack.begin(),
                                                 inqtEnd = inequalityPack.end();
         inqt != inqtEnd; ++inqt) {

      InequalityExpr *currIneq = *inqt;
      bool isOnFocusVarOnLeft = false;

      normalization(currExistVar, currIneq, isOnFocusVarOnLeft);

      // STEP 2b: divide the inequalityPack into separated packs
      // based on its operator existVar <= (lessThanPack), existVar >=
      // (greaterThanPack), existVar < (strictLessThanPack), existVar >
      // (strictGreaterThanPack)
      // or (nonePack) if there's no on focus exist variable in the equation.
      classification(currExistVar, currIneq, lessThanPack, greaterThanPack,
                     strictLessThanPack, strictGreaterThanPack, nonePack,
                     isOnFocusVarOnLeft);
    }

    // STEP 3: matching between inequality
    std::vector<InequalityExpr *> resultPack;
    resultPack = matching(lessThanPack, greaterThanPack, strictLessThanPack,
                          strictGreaterThanPack);
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
  llvm::errs() << "RESULT: ";
  result->dump();
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

    llvm::errs() << "PROCESSING INEQUALITY: ";
    currInequality->dump();

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
    llvm::errs() << "LEFT EXPR: ";
    leftExpr->dump();
    llvm::errs() << "RIGHT EXPR: ";
    rightExpr->dump();
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
    std::vector<InequalityExpr *> &strictLessThanPack,
    std::vector<InequalityExpr *> &strictGreaterThanPack,
    std::vector<InequalityExpr *> &nonePack, bool isOnFocusVarOnLeft) {
  if (!isOnFocusVarOnLeft) {
    nonePack.push_back(currIneq);
  } else {
    if (currIneq->getLeft().size() == 1) {
      if (currIneq->getKind() == Expr::Sle)
        lessThanPack.push_back(currIneq);
      else if (currIneq->getKind() == Expr::Sge)
        greaterThanPack.push_back(currIneq);
      else if (currIneq->getKind() == Expr::Slt)
        strictLessThanPack.push_back(currIneq);
      else if (currIneq->getKind() == Expr::Sgt)
        strictGreaterThanPack.push_back(currIneq);
    } else {
      nonePack.push_back(currIneq);
    }
  }
}

std::vector<InequalityExpr *> SubsumptionTableEntry::matching(
    std::vector<InequalityExpr *> lessThanPack,
    std::vector<InequalityExpr *> greaterThanPack,
    std::vector<InequalityExpr *> strictLessThanPack,
    std::vector<InequalityExpr *> strictGreaterThanPack) {

  std::vector<InequalityExpr *> result;

  // Given x <= expr1 and x >= expr2, we eliminate x with introducing the
  // constraint expr2 <= expr1.
  matchingLoop(Expr::Sle, greaterThanPack, lessThanPack, result);

  // Given x <= expr1 and x > expr2, we eliminate x with introducing the
  // constraint expr2 < expr1.
  matchingLoop(Expr::Slt, strictGreaterThanPack, lessThanPack, result);

  // Given x < expr1 and x >= expr2, we eliminate x with introducing the
  // constraint expr2 < expr1.
  matchingLoop(Expr::Slt, greaterThanPack, strictLessThanPack, result);

  // Given x < expr1 and x > expr2 , we eliminate x with introducing the
  // constraint expr2 < expr1.
  matchingLoop(Expr::Slt, strictGreaterThanPack, strictLessThanPack, result);

  return result;
}

void
SubsumptionTableEntry::matchingLoop(Expr::Kind kind,
                                    std::vector<InequalityExpr *> pack1,
                                    std::vector<InequalityExpr *> pack2,
                                    std::vector<InequalityExpr *> &result) {

  for (std::vector<InequalityExpr *>::iterator it1 = pack1.begin(),
                                               it1End = pack1.end();
       it1 != it1End; ++it1) {
    InequalityExpr *curr1 = *it1;

    for (std::vector<InequalityExpr *>::iterator it2 = pack2.begin(),
                                                 it2End = pack2.end();
         it2 != it2End; ++it2) {
      InequalityExpr *curr2 = *it2;

      std::map<ref<Expr>, int64_t> left = curr1->getRight();
      std::map<ref<Expr>, int64_t> right = curr2->getRight();
      simplifyMatching(left, right);
      if (left.size() > 0 && right.size() > 0) {
        InequalityExpr *ineq = new InequalityExpr(left, right, kind, NULL);
        result.push_back(ineq);
      }
    }
  }
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

  if (left.size() >= 1 && containsNonConstantExpr(left) && right.size() == 0) {
    right.insert(std::make_pair(
        ConstantExpr::alloc(0, left.begin()->first->getWidth()), 0));
  } else if (right.size() >= 1 && containsNonConstantExpr(right) &&
             left.size() == 0) {
    left.insert(std::make_pair(
        ConstantExpr::alloc(0, right.begin()->first->getWidth()), 0));
  }
}

bool SubsumptionTableEntry::containsNonConstantExpr(
    std::map<ref<Expr>, int64_t> map) {
  for (std::map<ref<Expr>, int64_t>::iterator mIt = map.begin(),
                                              mEnd = map.end();
       mIt != mEnd; ++mIt) {
    if (!llvm::isa<ConstantExpr>(mIt->first))
      return true;
  }
  return false;
}

void SubsumptionTableEntry::normalization(const Array *onFocusExistential,
                                          InequalityExpr *inequalityExpr,
                                          bool &isOnFocusVarOnLeft) {

  std::map<ref<Expr>, int64_t> left = inequalityExpr->getLeft();
  std::map<ref<Expr>, int64_t> right = inequalityExpr->getRight();

  int64_t onFocusVarCoefficient = 0;
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
        onFocusVarCoefficient = itLeft->second;
        isOnFocusVarOnLeft = true;
        ++itLeft;
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
        onFocusVarCoefficient = itRight->second;
        isOnFocusVarOnLeft = true;
        right.erase(itRight++);
      } else {
        ++itRight;
      }
    } else {
      ++itRight;
    }
  }

  // divide both sides with onFocusVariable coefficient.
  if (onFocusExistential &&
      (onFocusVarCoefficient != 1 && onFocusVarCoefficient != 0)) {
    std::map<ref<Expr>, int64_t>::iterator itLeft = left.begin();
    while (itLeft != left.end()) {
      itLeft->second = itLeft->second / onFocusVarCoefficient;
      ++itLeft;
    }

    std::map<ref<Expr>, int64_t>::iterator itRight = right.begin();
    while (itRight != right.end()) {
      itRight->second = itRight->second / onFocusVarCoefficient;
      ++itRight;
    }

    // if we divide with negative values, the Kind Expression would be reversed
    if (onFocusVarCoefficient < 0) {
      if (inequalityExpr->getKind() == Expr::Sle)
        inequalityExpr->updateKind(Expr::Sgt);
      else if (inequalityExpr->getKind() == Expr::Sge)
        inequalityExpr->updateKind(Expr::Slt);
      else if (inequalityExpr->getKind() == Expr::Slt)
        inequalityExpr->updateKind(Expr::Sge);
      else if (inequalityExpr->getKind() == Expr::Sgt)
        inequalityExpr->updateKind(Expr::Sle);
    }
  }

  if (left.size() >= 1 && containsNonConstantExpr(left) && right.size() == 0) {
    right.insert(std::make_pair(
        ConstantExpr::alloc(0, left.begin()->first->getWidth()), 0));
  } else if (right.size() >= 1 && containsNonConstantExpr(right) &&
             left.size() == 0) {
    left.insert(std::make_pair(
        ConstantExpr::alloc(0, right.begin()->first->getWidth()), 0));
  }

  if (left.size() > 0 && right.size() > 0) {
    inequalityExpr->updateLeft(left);
    inequalityExpr->updateRight(right);
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

      if (kind == Expr::Mul) {
        it2->second = it1->second * it2->second;
        isFound = true;
      } else if (kind == Expr::SDiv || kind == Expr::UDiv) {
        it2->second = it1->second / it2->second;
        isFound = true;
      } else if (kind == Expr::SRem || kind == Expr::URem) {
        it2->second = it1->second % it2->second;
        isFound = true;
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

ref<Expr>
SubsumptionTableEntry::simplifyArithmeticBody(ref<Expr> existsExpr,
                                              bool &hasExistentialsOnly) {
  assert(llvm::isa<ExistsExpr>(existsExpr));

  std::vector<ref<Expr> > interpolantPack;
  std::vector<ref<Expr> > equalityPack;

  ExistsExpr *expr = static_cast<ExistsExpr *>(existsExpr.get());

  // Assume the we shall return general ExistsExpr that does not contain
  // only existential variables.
  hasExistentialsOnly = false;

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

  if (fullEqualityConstraint->isFalse())
    return fullEqualityConstraint;

  // Try to simplify the interpolant. If the resulting simplification
  // was the constant true, then the equality constraints would contain
  // equality with constants only and no equality with shadow (existential)
  // variables, hence it should be safe to simply return the equality
  // constraint.
  interpolantPack.clear();
  ref<Expr> simplifiedInterpolant =
      simplifyInterpolantExpr(interpolantPack, body->getKid(0));
  if (simplifiedInterpolant->isTrue())
    return fullEqualityConstraint;

  if (fullEqualityConstraint->isTrue()) {
    // This is the case when the result is still an existentially-quantified
    // formula, but one that does not contain free variables.
    hasExistentialsOnly = !hasFree(expr->variables, simplifiedInterpolant);
    if (hasExistentialsOnly) {
      return existsExpr->rebuild(&simplifiedInterpolant);
    }
  }

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

        interpolantAtom = ShadowArray::createBinaryOfSameKind(
            interpolantAtom, newIntpLeft, newIntpRight);
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
    return ShadowArray::createBinaryOfSameKind(originalExpr, substituteExpr,
                                               originalExpr->getKid(1));

  if (originalExpr->getKid(1) == replacedExpr)
    return ShadowArray::createBinaryOfSameKind(
        originalExpr, originalExpr->getKid(0), substituteExpr);

  return ShadowArray::createBinaryOfSameKind(
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
    if (llvm::isa<EqExpr>(expr) && expr->getKid(0)->getWidth() == Expr::Bool &&
        expr->getKid(0)->isFalse()) {
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

  ref<Expr> simplifiedLhs = simplifyInterpolantExpr(interpolantPack, lhs);
  if (simplifiedLhs->isFalse())
    return simplifiedLhs;

  ref<Expr> simplifiedRhs = simplifyInterpolantExpr(interpolantPack, rhs);
  if (simplifiedRhs->isFalse())
    return simplifiedRhs;

  if (simplifiedLhs->isTrue())
    return simplifiedRhs;

  if (simplifiedRhs->isTrue())
    return simplifiedLhs;

  return AndExpr::alloc(simplifiedLhs, simplifiedRhs);
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

ref<Expr>
SubsumptionTableEntry::getSubstitution(ref<Expr> equalities,
                                       std::map<ref<Expr>, ref<Expr> > &map) {
  if (llvm::isa<EqExpr>(equalities.get())) {
    ref<Expr> lhs = equalities->getKid(0);
    if (llvm::isa<ReadExpr>(lhs.get()) || llvm::isa<ConcatExpr>(lhs.get())) {
      map[lhs] = equalities->getKid(1);
      return ConstantExpr::alloc(1, Expr::Bool);
    }
    return equalities;
  }

  if (llvm::isa<AndExpr>(equalities.get())) {
    ref<Expr> lhs = getSubstitution(equalities->getKid(0), map);
    ref<Expr> rhs = getSubstitution(equalities->getKid(1), map);
    if (lhs->isTrue()) {
      if (rhs->isTrue()) {
        return ConstantExpr::alloc(1, Expr::Bool);
      }
      return rhs;
    } else {
      if (rhs->isTrue()) {
        return lhs;
      }
      return AndExpr::alloc(lhs, rhs);
    }
  }
  return equalities;
}

ref<Expr> SubsumptionTableEntry::simplifyExistsExpr(ref<Expr> existsExpr,
                                                    bool &hasExistentialsOnly) {
  assert(llvm::isa<ExistsExpr>(existsExpr.get()));

  ref<Expr> body = llvm::dyn_cast<ExistsExpr>(existsExpr.get())->body;
  assert(llvm::isa<AndExpr>(body.get()));

  std::map<ref<Expr>, ref<Expr> > substitution;
  ref<Expr> equalities = getSubstitution(body->getKid(1), substitution);
  ref<Expr> interpolant =
      ApplySubstitutionVisitor(substitution).visit(body->getKid(0));
  ref<Expr> newBody = AndExpr::alloc(interpolant, equalities);
  ref<Expr> ret = simplifyArithmeticBody(existsExpr->rebuild(&newBody),
                                         hasExistentialsOnly);
  return ret;
}

bool SubsumptionTableEntry::subsumed(TimingSolver *solver,
                                     ExecutionState &state, double timeout) {

  // Quick check for subsumption in case the interpolant is empty
  if (empty())
    return true;

  std::map<llvm::Value *, ref<Expr> > stateSingletonStore =
      state.itreeNode->getSingletonExpressions();
  std::map<llvm::Value *, std::vector<ref<Expr> > > stateCompositeStore =
      state.itreeNode->getCompositeExpressions();

  ref<Expr> stateEqualityConstraints;
  for (std::vector<llvm::Value *>::iterator it = singletonStoreKeys.begin(),
                                            itEnd = singletonStoreKeys.end();
       it != itEnd; ++it) {
    const ref<Expr> lhs = singletonStore[*it];
    const ref<Expr> rhs = stateSingletonStore[*it];

    // If the current state does not constrain the same
    // allocation, subsumption fails.
    if (!rhs.get())
      return false;

    stateEqualityConstraints = (!stateEqualityConstraints.get()
                                    ? EqExpr::alloc(lhs, rhs)
                                    : AndExpr::alloc(EqExpr::alloc(lhs, rhs),
                                                     stateEqualityConstraints));
  }

  for (std::vector<llvm::Value *>::iterator it = compositeStoreKeys.begin(),
                                            itEnd = compositeStoreKeys.end();
       it != itEnd; ++it) {
    std::vector<ref<Expr> > lhsList = compositeStore[*it];
    std::vector<ref<Expr> > rhsList = stateCompositeStore[*it];

    // If the current state does not constrain the same
    // allocation, subsumption fails.
    if (rhsList.empty())
      return false;

    ref<Expr> auxDisjuncts;
    bool auxDisjunctsEmpty = true;

    for (std::vector<ref<Expr> >::iterator lhsIter = lhsList.begin(),
                                           lhsIterEnd = lhsList.end();
         lhsIter != lhsIterEnd; ++lhsIter) {

      for (std::vector<ref<Expr> >::iterator rhsIter = rhsList.begin(),
                                             rhsIterEnd = rhsList.end();
           rhsIter != rhsIterEnd; ++rhsIter) {

        ref<Expr> lhs = *lhsIter;
        ref<Expr> rhs = *rhsIter;

        // FIXME: This is a quick hack that was temporarily required due
        // to field insensitivity of the dependency analysis, such that
        // allocations are matched if they had the same base address even
        // though they point to different locations in the composite.
        if (lhs->getWidth() > rhs->getWidth()) {
          rhs = ZExtExpr::alloc(rhs, lhs->getWidth());
        } else if (lhs->getWidth() < rhs->getWidth()) {
          lhs = ZExtExpr::alloc(lhs, rhs->getWidth());
        }

        if (llvm::isa<ConstantExpr>(lhs) && llvm::isa<ConstantExpr>(rhs)) {
          if (lhs.operator==(rhs)) {
            // Because if the disjunct is TRUE, then the disjunction is true
            auxDisjuncts = ConstantExpr::alloc(1, Expr::Bool);
            // To break from outer loop as well
            goto end_loop;
          }
        }

        if (auxDisjunctsEmpty) {
          auxDisjuncts = EqExpr::alloc(lhs, rhs);
          auxDisjunctsEmpty = false;
        } else {
          auxDisjuncts = OrExpr::alloc(EqExpr::alloc(lhs, rhs), auxDisjuncts);
        }
      }
    }
  end_loop:

    if (!auxDisjunctsEmpty) {
      stateEqualityConstraints =
          stateEqualityConstraints.get()
              ? AndExpr::alloc(auxDisjuncts, stateEqualityConstraints)
              : auxDisjuncts;
    }
  }

  // We create path condition needed constraints marking structure
  std::map<Expr *, PathConditionMarker *> markerMap =
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

  bool queryHasNoFreeVariables = false;

  if (!existentials.empty()) {
    ref<Expr> existsExpr = ExistsExpr::create(existentials, query);
    //     llvm::errs() << "Before simplification:\n";
    //     ExprPPrinter::printQuery(llvm::errs(), state.constraints,
    // existsExpr);
    query = simplifyExistsExpr(existsExpr, queryHasNoFreeVariables);
  }

  // If query simplification result was false, we quickly fail without calling
  // the solver
  if (query->isFalse())
    return false;

  bool success = false;

  Z3Solver *z3solver = 0;

  // We call the solver only when the simplified query is
  // not a constant.
  if (!llvm::isa<ConstantExpr>(query)) {
    //    llvm::errs() << "Querying for subsumption check:\n";
    //    ExprPPrinter::printQuery(llvm::errs(), state.constraints, query);
    ++checkSolverCount;
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

      if (queryHasNoFreeVariables) {
        // In case the query has no free variables, we reformulate
        // the solver call as satisfiability check of the body of
        // the query.
        ConstraintManager constraints;
        ref<ConstantExpr> tmpExpr;

        ref<Expr> falseExpr = ConstantExpr::alloc(0, Expr::Bool);
        constraints.addConstraint(EqExpr::alloc(falseExpr, query->getKid(0)));

        // llvm::errs() << "Querying for satisfiability check:\n";
        // ExprPPrinter::printQuery(llvm::errs(), constraints, falseExpr);

        actualSolverCallTimer.start();
        success = z3solver->getValue(Query(constraints, falseExpr), tmpExpr);
        // double elapsedTime =
        actualSolverCallTimer.stop();

        result = success ? Solver::True : Solver::Unknown;

      } else {
        // llvm::errs() << "Querying for subsumption check:\n";
        // ExprPPrinter::printQuery(llvm::errs(), state.constraints, query);

        actualSolverCallTimer.start();
        success = z3solver->directComputeValidity(
            Query(state.constraints, query), result);
        // double elapsedTime =
        actualSolverCallTimer.stop();

        //        if (elapsedTime > expectedMaxElapsedTime) {
        //            llvm::errs() << "LONG QUERY 2:" << "\n";
        //            Query(state.constraints, query).dump();
        //        }
      }

      z3solver->setCoreSolverTimeout(0);

    } else {
      // llvm::errs() << "No existential\n";

      // llvm::errs() << "Querying for subsumption check:\n";
      // ExprPPrinter::printQuery(llvm::errs(), state.constraints, query);

      // We call the solver in the standard way if the
      // formula is unquantified.
      solver->setTimeout(timeout);
      actualSolverCallTimer.start();
      success = solver->evaluate(state, query, result);
      // double elapsedTime =
      actualSolverCallTimer.stop();

      //      if (elapsedTime > expectedMaxElapsedTime) {
      //          llvm::errs() << "LONG QUERY 3:" << "\n";
      //          Query(state.constraints, query).dump();
      //      }

      solver->setTimeout(0);
    }
  } else {
    if (query->isTrue())
      return true;
    return false;
  }

  if (success && result == Solver::True) {
    //         llvm::errs() << "Solver decided validity\n";
    std::vector<ref<Expr> > unsatCore;
    if (z3solver) {
      unsatCore = z3solver->getUnsatCore();
      delete z3solver;
    } else {
      unsatCore = solver->getUnsatCore();
    }

    for (std::vector<ref<Expr> >::iterator it1 = unsatCore.begin();
         it1 != unsatCore.end(); it1++) {
      // FIXME: Sometimes some constraints are not in the PC. This is
      // because constraints are not properly added at state merge.
      PathConditionMarker *marker = markerMap[it1->get()];
      if (marker)
        marker->setAsMaybeCore();
    }

  } else {
    // Here the solver could not decide that the subsumption is valid.
    // It may have decided invalidity, however,
    // CexCachingSolver::computeValidity,
    // which was eventually called from solver->evaluate
    // is conservative, where it returns Solver::Unknown even in case when
    // invalidity is established by the solver.
    // llvm::errs() << "Solver did not decide validity\n";

    ++checkSolverFailureCount;
    if (z3solver)
      delete z3solver;

    return false;
  }

  // State subsumed, we mark needed constraints on the
  // path condition.
  AllocationGraph *g = new AllocationGraph();
  for (std::map<Expr *, PathConditionMarker *>::iterator
           it = markerMap.begin(),
           itEnd = markerMap.end();
       it != itEnd; it++) {
    // FIXME: Sometimes some constraints are not in the PC. This is
    // because constraints are not properly added at state merge.
    if (it->second)
      it->second->setAsCore(g);
  }
  ITreeNode::deleteMarkerMap(markerMap);
  // llvm::errs() << "AllocationGraph\n";
  // g->dump();

  // We mark memory allocations needed for the unsatisfiabilty core
  state.itreeNode->computeCoreAllocations(g);

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

void SubsumptionTableEntry::printStat(llvm::raw_ostream &stream) {
  stream << "KLEE: done:     Time for actual solver calls in subsumption check "
            "(ms) = " << actualSolverCallTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     Number of solver calls for subsumption check "
            "(failed) = " << checkSolverCount << " (" << checkSolverFailureCount
         << ")\n";
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

void InequalityExpr::updateKind(Expr::Kind newKind) { kind = newKind; }

void InequalityExpr::print(llvm::raw_ostream &stream) const {
  stream << "(" << kind << " (";
  for (std::map<ref<Expr>, int64_t>::const_iterator it = left.begin(),
                                                    itEnd = left.end();
       it != itEnd; ++it) {
    stream << "(" << it->second << ") ";
    it->first->print(stream);
    if (it != --left.end()) {
      stream << " + ";
    }
  }
  stream << ") (";
  for (std::map<ref<Expr>, int64_t>::const_iterator it = right.begin(),
                                                    itEnd = right.end();
       it != itEnd; ++it) {
    stream << "(" << it->second << ") ";
    it->first->print(stream);
    if (it != --right.end()) {
      stream << " + ";
    }
  }
  stream << "))";
}

/**/

StatTimer ITree::setCurrentINodeTimer;
StatTimer ITree::removeTimer;
StatTimer ITree::checkCurrentStateSubsumptionTimer;
StatTimer ITree::markPathConditionTimer;
StatTimer ITree::splitTimer;
StatTimer ITree::executeOnNodeTimer;

void ITree::printTimeStat(llvm::raw_ostream &stream) {
  stream << "KLEE: done:     setCurrentINode = " << setCurrentINodeTimer.get() *
                                                        1000 << "\n";
  stream << "KLEE: done:     remove = " << removeTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     checkCurrentStateSubsumption = "
         << checkCurrentStateSubsumptionTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     markPathCondition = "
         << markPathConditionTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     split = " << splitTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     executeOnNode = " << executeOnNodeTimer.get() *
                                                      1000 << "\n";
}

void ITree::printTableStat(llvm::raw_ostream &stream) const {
  double programPointNumber = 0.0, entryNumber = 0.0;
  for (std::map<uintptr_t,
                std::vector<SubsumptionTableEntry *> >::const_iterator
           it = subsumptionTable.begin(),
           itEnd = subsumptionTable.end();
       it != itEnd; ++it) {
    if (!it->second.empty()) {
      entryNumber += it->second.size();
      ++programPointNumber;
    }
  }
  stream << "KLEE: done:     Table entry per checkpoint instruction = "
         << (entryNumber / programPointNumber) << "\n";
  SubsumptionTableEntry::printStat(stream);
}

void ITree::dumpInterpolationStat() {
  bool useColors = llvm::errs().is_displayed();
  if (useColors)
    llvm::errs().changeColor(llvm::raw_ostream::GREEN,
                             /*bold=*/true,
                             /*bg=*/false);
  llvm::errs() << "\nKLEE: done: Subsumption statistics\n";
  printTableStat(llvm::errs());
  llvm::errs() << "KLEE: done: ITree method execution times (ms):\n";
  printTimeStat(llvm::errs());
  llvm::errs() << "KLEE: done: ITreeNode method execution times (ms):\n";
  ITreeNode::printTimeStat(llvm::errs());
  if (useColors)
    llvm::errs().resetColor();
}

ITree::ITree(ExecutionState *_root) {
  currentINode = 0;
  if (!_root->itreeNode) {
    currentINode = new ITreeNode(0);
  }
  root = currentINode;
}

ITree::~ITree() {
  for (std::map<uintptr_t, std::vector<SubsumptionTableEntry *> >::iterator
           it = subsumptionTable.begin(),
           itEnd = subsumptionTable.end();
       it != itEnd; ++it) {
    for (std::vector<SubsumptionTableEntry *>::iterator
             it1 = it->second.begin(),
             it1End = it->second.end();
         it1 != it1End; ++it1) {
      delete *it1;
    }
    it->second.clear();
  }
  subsumptionTable.clear();
}

bool ITree::checkCurrentStateSubsumption(TimingSolver *solver,
                                         ExecutionState &state,
                                         double timeout) {
  assert(state.itreeNode == currentINode);

  // Immediately return if the state's instruction is not the
  // the interpolation node id. The interpolation node id is the
  // first instruction executed of the sequence executed for a state
  // node, typically this the first instruction of a basic block.
  // Subsumption check only matches against this first instruction.
  if (!state.itreeNode || reinterpret_cast<uintptr_t>(state.pc->inst) !=
                              state.itreeNode->getNodeId())
    return false;

  checkCurrentStateSubsumptionTimer.start();
  std::vector<SubsumptionTableEntry *> entryList =
      subsumptionTable[state.itreeNode->getNodeId()];

  if (entryList.empty())
    return false;

  for (std::vector<SubsumptionTableEntry *>::iterator it = entryList.begin(),
                                                      itEnd = entryList.end();
       it != itEnd; ++it) {
    if ((*it)->subsumed(solver, state, timeout)) {
      // We mark as subsumed such that the node will not be
      // stored into table (the table already contains a more
      // general entry).
      currentINode->isSubsumed = true;

      // Mark the node as subsumed, and create a subsumption edge
      SearchTree::markAsSubsumed(currentINode, (*it));
      checkCurrentStateSubsumptionTimer.stop();
      return true;
    }
  }
  checkCurrentStateSubsumptionTimer.stop();
  return false;
}

void ITree::store(SubsumptionTableEntry *subItem) {
  subsumptionTable[subItem->nodeId].push_back(subItem);
}

void ITree::setCurrentINode(ExecutionState &state, uintptr_t programPoint) {
  setCurrentINodeTimer.start();
  currentINode = state.itreeNode;
  currentINode->setNodeLocation(programPoint);
  SearchTree::setCurrentNode(state, programPoint);
  setCurrentINodeTimer.stop();
}

void ITree::remove(ITreeNode *node) {
  removeTimer.start();
  assert(!node->left && !node->right);
  do {
    ITreeNode *p = node->parent;

    // As the node is about to be deleted, it must have been completely
    // traversed, hence the correct time to table the interpolant.
    if (!node->isSubsumed) {
      SubsumptionTableEntry *entry = new SubsumptionTableEntry(node);
      store(entry);
      SearchTree::addTableEntryMapping(node, entry);
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
  removeTimer.stop();
}

std::pair<ITreeNode *, ITreeNode *>
ITree::split(ITreeNode *parent, ExecutionState *left, ExecutionState *right) {
  splitTimer.start();
  parent->split(left, right);
  SearchTree::addChildren(parent, parent->left, parent->right);
  std::pair<ITreeNode *, ITreeNode *> ret(parent->left, parent->right);
  splitTimer.stop();
  return ret;
}

void ITree::markPathCondition(ExecutionState &state, TimingSolver *solver) {
  markPathConditionTimer.start();
  std::vector<ref<Expr> > unsatCore = solver->getUnsatCore();

  AllocationGraph *g = new AllocationGraph();

  llvm::BranchInst *binst =
      llvm::dyn_cast<llvm::BranchInst>(state.prevPC->inst);
  if (binst) {
    currentINode->dependency->markAllValues(g, binst->getCondition());
  }

  PathCondition *pc = currentINode->pathCondition;

  if (pc != 0) {
    for (std::vector<ref<Expr> >::iterator it = unsatCore.begin(),
                                           itEnd = unsatCore.end();
         it != itEnd; ++it) {
      for (; pc != 0; pc = pc->cdr()) {
        if (pc->car().compare(it->get()) == 0) {
          pc->setAsCore(g);
          pc = pc->cdr();
          break;
        }
      }
    }
  }

  // llvm::errs() << "AllocationGraph\n";
  // g->dump();

  // Compute memory allocations needed by the unsatisfiability core
  currentINode->computeCoreAllocations(g);

  delete g; // Delete the AllocationGraph object
  markPathConditionTimer.stop();
}

void ITree::execute(llvm::Instruction *instr) {
  std::vector<ref<Expr> > dummyArgs;
  executeOnNode(currentINode, instr, dummyArgs);
}

void ITree::execute(llvm::Instruction *instr, ref<Expr> arg1) {
  std::vector<ref<Expr> > args;
  args.push_back(arg1);
  executeOnNode(currentINode, instr, args);
}

void ITree::execute(llvm::Instruction *instr, ref<Expr> arg1, ref<Expr> arg2) {
  std::vector<ref<Expr> > args;
  args.push_back(arg1);
  args.push_back(arg2);
  executeOnNode(currentINode, instr, args);
}

void ITree::execute(llvm::Instruction *instr, ref<Expr> arg1, ref<Expr> arg2,
                    ref<Expr> arg3) {
  std::vector<ref<Expr> > args;
  args.push_back(arg1);
  args.push_back(arg2);
  args.push_back(arg3);
  executeOnNode(currentINode, instr, args);
}

void ITree::execute(llvm::Instruction *instr, std::vector<ref<Expr> > &args) {
  executeOnNode(currentINode, instr, args);
}

void ITree::executeOnNode(ITreeNode *node, llvm::Instruction *instr,
                          std::vector<ref<Expr> > &args) {
  executeOnNodeTimer.start();
  node->execute(instr, args);
  executeOnNodeTimer.stop();
}

void ITree::printNode(llvm::raw_ostream &stream, ITreeNode *n,
                      std::string edges) const {
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

void ITree::print(llvm::raw_ostream &stream) const {
  stream << "------------------------- ITree Structure "
            "---------------------------\n";
  stream << this->root->nodeId;
  if (this->root == this->currentINode) {
    stream << " (active)";
  }
  this->printNode(stream, this->root, "");
  stream << "\n------------------------- Subsumption Table "
            "-------------------------\n";
  for (std::map<uintptr_t,
                std::vector<SubsumptionTableEntry *> >::const_iterator
           it = subsumptionTable.begin(),
           itEnd = subsumptionTable.end();
       it != itEnd; ++it) {
    for (std::vector<SubsumptionTableEntry *>::const_iterator
             it1 = it->second.begin(),
             it1End = it->second.end();
         it1 != it1End; ++it1) {
      (*it1)->print(stream);
    }
  }
}

void ITree::dump() const { this->print(llvm::errs()); }

/**/

// Statistics
StatTimer ITreeNode::getInterpolantTimer;
StatTimer ITreeNode::addConstraintTimer;
StatTimer ITreeNode::splitTimer;
StatTimer ITreeNode::makeMarkerMapTimer;
StatTimer ITreeNode::deleteMarkerMapTimer;
StatTimer ITreeNode::executeTimer;
StatTimer ITreeNode::bindCallArgumentsTimer;
StatTimer ITreeNode::popAbstractDependencyFrameTimer;
StatTimer ITreeNode::getSingletonExpressionsTimer;
StatTimer ITreeNode::getCompositeExpressionsTimer;
StatTimer ITreeNode::getSingletonCoreExpressionsTimer;
StatTimer ITreeNode::getCompositeCoreExpressionsTimer;
StatTimer ITreeNode::computeCoreAllocationsTimer;

void ITreeNode::printTimeStat(llvm::raw_ostream &stream) {
  stream << "KLEE: done:     getInterpolant = " << getInterpolantTimer.get() *
                                                       1000 << "\n";
  stream << "KLEE: done:     addConstraintTime = " << addConstraintTimer.get() *
                                                          1000 << "\n";
  stream << "KLEE: done:     splitTime = " << splitTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     makeMarkerMap = " << makeMarkerMapTimer.get() *
                                                      1000 << "\n";
  stream << "KLEE: done:     deleteMarkerMap = " << deleteMarkerMapTimer.get() *
                                                        1000 << "\n";
  stream << "KLEE: done:     execute = " << executeTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     bindCallArguments = "
         << bindCallArgumentsTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     popAbstractDependencyFrame = "
         << popAbstractDependencyFrameTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     getSingletonExpressions = "
         << getSingletonExpressionsTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     getCompositeExpressions = "
         << getCompositeExpressionsTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     getSingletonCoreCoreExpressions = "
         << getSingletonCoreExpressionsTimer.get() << "\n";
  stream << "KLEE: done:     getCompositeCoreExpressions = "
         << getCompositeCoreExpressionsTimer.get() * 1000 << "\n";
  stream << "KLEE: done:     computeCoreAllocations = "
         << computeCoreAllocationsTimer.get() * 1000 << "\n";
}

ITreeNode::ITreeNode(ITreeNode *_parent)
    : parent(_parent), left(0), right(0), nodeId(0), isSubsumed(false),
      graph(_parent ? _parent->graph : 0) {

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
  ITreeNode::getInterpolantTimer.start();
  ref<Expr> expr = this->pathCondition->packInterpolant(replacements);
  ITreeNode::getInterpolantTimer.stop();
  return expr;
}

void ITreeNode::addConstraint(ref<Expr> &constraint, llvm::Value *condition) {
  ITreeNode::addConstraintTimer.start();
  pathCondition =
      new PathCondition(constraint, dependency, condition, pathCondition);
  graph->addPathCondition(this, pathCondition, constraint);
  ITreeNode::addConstraintTimer.stop();
}

void ITreeNode::split(ExecutionState *leftData, ExecutionState *rightData) {
  ITreeNode::splitTimer.start();
  assert(left == 0 && right == 0);
  leftData->itreeNode = left = new ITreeNode(this);
  rightData->itreeNode = right = new ITreeNode(this);
  ITreeNode::splitTimer.stop();
}

std::map<Expr *, PathConditionMarker *> ITreeNode::makeMarkerMap() const {
  ITreeNode::makeMarkerMapTimer.start();
  std::map<Expr *, PathConditionMarker *> result;
  for (PathCondition *it = pathCondition; it != 0; it = it->cdr()) {
    PathConditionMarker *marker = new PathConditionMarker(it);
    if (llvm::isa<OrExpr>(it->car().get())) {
      // FIXME: Break up disjunction into its components, because each disjunct
      // is solved separately. The or constraint was due to state merge.
      // Hence, the following is just a makeshift for when state merge is
      // properly implemented.
      result[it->car()->getKid(0).get()] = new PathConditionMarker(it);
      result[it->car()->getKid(1).get()] = new PathConditionMarker(it);
    }
    result[it->car().get()] = marker;
  }
  ITreeNode::makeMarkerMapTimer.stop();
  return result;
}

void
ITreeNode::deleteMarkerMap(std::map<Expr *, PathConditionMarker *> &markerMap) {
  ITreeNode::deleteMarkerMapTimer.start();
  for (std::map<Expr *, PathConditionMarker *>::iterator
           it = markerMap.begin(),
           itEnd = markerMap.end();
       it != itEnd; ++it) {
    if (it->second)
      delete it->second;
  }
  markerMap.clear();
  ITreeNode::deleteMarkerMapTimer.stop();
}

void ITreeNode::execute(llvm::Instruction *instr,
                        std::vector<ref<Expr> > &args) {
  executeTimer.start();
  dependency->execute(instr, args);
  executeTimer.stop();
}

void ITreeNode::bindCallArguments(llvm::Instruction *site,
                                  std::vector<ref<Expr> > &arguments) {
  ITreeNode::bindCallArgumentsTimer.start();
  dependency->bindCallArguments(site, arguments);
  ITreeNode::bindCallArgumentsTimer.stop();
}

void ITreeNode::popAbstractDependencyFrame(llvm::CallInst *site,
                                           llvm::Instruction *inst,
                                           ref<Expr> returnValue) {
  // TODO: This is probably where we should simplify
  // the dependency graph by removing callee values.
  ITreeNode::popAbstractDependencyFrameTimer.start();
  dependency->bindReturnValue(site, inst, returnValue);
  ITreeNode::popAbstractDependencyFrameTimer.stop();
}

std::map<llvm::Value *, ref<Expr> > ITreeNode::getSingletonExpressions() const {
  ITreeNode::getSingletonExpressionsTimer.start();
  std::map<llvm::Value *, ref<Expr> > ret;
  std::vector<const Array *> dummyReplacements;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getSingletonExpressions(dummyReplacements, false);
  ITreeNode::getSingletonExpressionsTimer.stop();
  return ret;
}

std::map<llvm::Value *, std::vector<ref<Expr> > >
ITreeNode::getCompositeExpressions() const {
  ITreeNode::getCompositeExpressionsTimer.start();
  std::map<llvm::Value *, std::vector<ref<Expr> > > ret;
  std::vector<const Array *> dummyReplacements;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getCompositeExpressions(dummyReplacements, false);
  ITreeNode::getCompositeExpressionsTimer.stop();
  return ret;
}

std::map<llvm::Value *, ref<Expr> > ITreeNode::getSingletonCoreExpressions(
    std::vector<const Array *> &replacements) const {
  ITreeNode::getSingletonCoreExpressionsTimer.start();
  std::map<llvm::Value *, ref<Expr> > ret;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getSingletonExpressions(replacements, true);
  ITreeNode::getSingletonCoreExpressionsTimer.stop();
  return ret;
}

std::map<llvm::Value *, std::vector<ref<Expr> > >
ITreeNode::getCompositeCoreExpressions(std::vector<const Array *> &replacements)
    const {
  ITreeNode::getCompositeCoreExpressionsTimer.start();
  std::map<llvm::Value *, std::vector<ref<Expr> > > ret;

  // Since a program point index is a first statement in a basic block,
  // the allocations to be stored in subsumption table should be obtained
  // from the parent node.
  if (parent)
    ret = parent->dependency->getCompositeExpressions(replacements, true);
  ITreeNode::getCompositeCoreExpressionsTimer.stop();
  return ret;
}

void ITreeNode::computeCoreAllocations(AllocationGraph *g) {
  ITreeNode::computeCoreAllocationsTimer.start();
  dependency->computeCoreAllocations(g);
  ITreeNode::computeCoreAllocationsTimer.stop();
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
