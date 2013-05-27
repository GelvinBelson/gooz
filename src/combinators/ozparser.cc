#include "combinators/ozparser.h"

#include "base/escaping.h"
#include "base/macros.h"
#include "base/stl-util.h"

namespace combinators { namespace oz {

// -----------------------------------------------------------------------------

// Describes an Oz rule which has a begin and an end token.
struct OzRule {
  OzLexemType begin;
  OzLexemType end;

  OzRule() {}
  OzRule(const OzRule& rule)
      : begin(rule.begin),
        end(rule.end) {}

  OzRule(OzLexemType begin_,
         OzLexemType end_)
      : begin(begin_),
        end(end_) {}
};

struct OzSubRule {
  UnorderedSet<OzLexemType, OzLexemTypeHash> separators;

  OzSubRule() {}

  OzSubRule(const OzLexemType branches[]) {
    for (uint i = 0; branches[i] != OzLexemType::INVALID; ++i) {
      separators.insert(branches[i]);
    }
  }
};

// -----------------------------------------------------------------------------

// Top-level keywords
const OzRule kOzRules[] = {
  OzRule(OzLexemType::CASE, OzLexemType::END),
  OzRule(OzLexemType::CLASS, OzLexemType::END),
  OzRule(OzLexemType::FOR, OzLexemType::END),
  OzRule(OzLexemType::FUN, OzLexemType::END),
  OzRule(OzLexemType::FUNCTOR, OzLexemType::END),
  OzRule(OzLexemType::IF, OzLexemType::END),
  OzRule(OzLexemType::LOCAL, OzLexemType::END),
  OzRule(OzLexemType::LOCK, OzLexemType::END),
  OzRule(OzLexemType::METH, OzLexemType::END),
  OzRule(OzLexemType::PROC, OzLexemType::END),
  OzRule(OzLexemType::RAISE, OzLexemType::END),
  OzRule(OzLexemType::THREAD, OzLexemType::END),
  OzRule(OzLexemType::TRY, OzLexemType::END),

  OzRule(OzLexemType::CALL_BEGIN, OzLexemType::CALL_END),
  OzRule(OzLexemType::LIST_BEGIN, OzLexemType::LIST_END),
  OzRule(OzLexemType::BEGIN_LPAREN, OzLexemType::END_RPAREN),
  OzRule(OzLexemType::BEGIN_RECORD_FEATURES, OzLexemType::END_RPAREN),
};

struct OzSchema {
  UnorderedMap<OzLexemType, OzRule, OzLexemTypeHash> rules;
  UnorderedSet<OzLexemType, OzLexemTypeHash> end_tokens;

  vector<OzLexemType> class_branches;
  vector<OzLexemType> cond_branches;
  vector<OzLexemType> cond_case_branches;
  vector<OzLexemType> cond_if_branches;
  vector<OzLexemType> functor_branches;
  vector<OzLexemType> local_branches;
  vector<OzLexemType> try_branches;

  OzSchema() {
    for (uint i = 0; i < ArraySize(kOzRules); ++i) {
      const OzRule& rule = kOzRules[i];
      rules[rule.begin] = rule;
      end_tokens.insert(rule.end);
    }

    local_branches.push_back(OzLexemType::IN);

    cond_branches.push_back(OzLexemType::ELSEIF);
    cond_branches.push_back(OzLexemType::ELSECASE);
    cond_branches.push_back(OzLexemType::ELSE);

    cond_if_branches.push_back(OzLexemType::THEN);

    cond_case_branches.push_back(OzLexemType::OF);
    cond_case_branches.push_back(OzLexemType::ELSEOF);

    functor_branches.push_back(OzLexemType::EXPORT);
    functor_branches.push_back(OzLexemType::REQUIRE);
    functor_branches.push_back(OzLexemType::PREPARE);
    functor_branches.push_back(OzLexemType::IMPORT);
    functor_branches.push_back(OzLexemType::DEFINE);

    class_branches.push_back(OzLexemType::FROM);
    class_branches.push_back(OzLexemType::PROP);
    class_branches.push_back(OzLexemType::FEAT);
    class_branches.push_back(OzLexemType::ATTR);
    class_branches.push_back(OzLexemType::METH);

    try_branches.push_back(OzLexemType::CATCH);
    try_branches.push_back(OzLexemType::FINALLY);
  }
};

const OzSchema kOzSchema;

// -----------------------------------------------------------------------------

TopLevelScopeParser::TopLevelScopeParser()
    : midlevel_parser_(new MidLevelScopeParser) {
}

TopLevelScopeParser::TopLevelScopeParser(MidLevelScopeParser* midlevel_parser)
    : midlevel_parser_(midlevel_parser) {
}

ParsingResult<OzLexemStream>
TopLevelScopeParser::Parse(const OzLexemStream& lexems,
                           shared_ptr<AbstractOzNode>& root) {
  shared_ptr<OzNodeGeneric> toplevel(new OzNodeGeneric(lexems));
  toplevel->SetType(OzLexemType::TOP_LEVEL);
  ParsingResult<OzLexemStream> res = ParseInternal(lexems, toplevel.get());
  if (res.status == ParsingStatus::OK) {
    if (res.next.StreamEmpty()) {
      root = ((midlevel_parser_ != nullptr)
              ? midlevel_parser_->Parse(toplevel)
              : toplevel);
      // TODO: Check for OzNodeError in the AST

    } else {
      res.Fail((boost::format("Unexpected end token: %s")
                % Str(res.next.first())).str());
    }
  }
  return res;
}

ParsingResult<OzLexemStream>
TopLevelScopeParser::ParseInternal(const OzLexemStream& lexems,
                                   OzNodeGeneric* const root) {
  CHECK_NOTNULL(root);
  ParsingResult<OzLexemStream> result(lexems);

  OzLexemStream stream = lexems;
  while (!stream.StreamEmpty()) {
    const OzLexem& token = stream.first();

    // End branch token?
    if (ContainsKey(kOzSchema.end_tokens, token.exact_type))
      return result.Succeed(stream);

    // New branch token?
    const OzRule* rule = FindOrNull(kOzSchema.rules, token.exact_type);
    if (rule != nullptr) {
      shared_ptr<OzNodeGeneric> branch(new OzNodeGeneric(stream));
      branch->SetType(token.type);
      ParsingResult<OzLexemStream> rec_result =
          ParseInternal(stream.Slice(1), branch.get());
      if (rec_result.status == ParsingStatus::FAILED) {
        result.Fail();
        std::swap(result.errors, rec_result.errors);
        return result;
      }

      stream = rec_result.next;
      if (stream.StreamEmpty()) {
        result.Fail();
        auto fmt = boost::format(
            "Reached end of input and could not find end token for %s");
        result.errors.push_back((fmt % Str(token)).str());
        return result;
      }

      const OzLexem& end_token = stream.first();
      if (end_token.exact_type != rule->end) {
        result.Fail();
        auto fmt = boost::format(
            "End token %s does not match expectations to end %s");
        result.errors.push_back((fmt % Str(end_token) % Str(token)).str());
        return result;
      }
      stream = stream.Slice(1);
      branch->tokens = OzLexemStream(branch->tokens, stream);

      root->nodes.push_back(
          (midlevel_parser_ != nullptr)
          ? midlevel_parser_->Parse(branch)
          : branch);

    } else {
      // Not a scope start: wrap lexem as an OzNode and append
      if (token.type == OzLexemType::VARIABLE)
        root->nodes.push_back(
            shared_ptr<AbstractOzNode>(new OzNodeVar(stream.Slice(0, 1))));
      else
        root->nodes.push_back(
            shared_ptr<AbstractOzNode>(new OzNode(stream.Slice(0, 1))));
      stream = stream.Slice(1);
    }
  }

  return result.Succeed(stream);  // end of stream
}

// -----------------------------------------------------------------------------

void SplitNodes(const vector<shared_ptr<AbstractOzNode> >& nodes,
                const vector<OzLexemType>& edges,
                vector<int>* edge_pos) {
  CHECK_NOTNULL(edge_pos);
  CHECK_GT(edges.size(), 0UL);
  for (uint i = 0; i < nodes.size(); ++i)
    if (std::find(edges.begin(), edges.end(), nodes[i]->type) != edges.end())
      edge_pos->push_back(i);
}

// -----------------------------------------------------------------------------

MidLevelScopeParser::MidLevelScopeParser()
    : expr_parser_(new ExpressionParser) {
}

MidLevelScopeParser::MidLevelScopeParser(ExpressionParser* expr_parser)
    : expr_parser_(expr_parser) {
}

// Makes a slice of Oz nodes: nodes[begin ... end[
OzNodeGeneric* OzNodeSlice(const vector<shared_ptr<AbstractOzNode> >& nodes,
                           int ibegin, int iend) {
  OzNodeGeneric* const generic =
      new OzNodeGeneric(OzLexemSlice(*nodes[ibegin], *nodes[iend - 1]));
  generic->nodes.insert(generic->nodes.begin(),
                        nodes.begin() + ibegin,
                        nodes.begin() + iend);
  return generic;
}

shared_ptr<AbstractOzNode>
MidLevelScopeParser::ParseLocal(shared_ptr<OzNodeGeneric>& root) {
  vector<int> edge_pos;
  SplitNodes(root->nodes, kOzSchema.local_branches, &edge_pos);

  switch(edge_pos.size()) {
    case 0: return root;
    case 1: {
      const int in_pos = edge_pos[0];
      shared_ptr<OzNodeLocal> local(new OzNodeLocal(*root));
      local->defs.reset(OzNodeSlice(root->nodes, 0, in_pos));
      local->body.reset(
          OzNodeSlice(root->nodes, in_pos + 1, root->nodes.size()));

      if (expr_parser_ != nullptr) {
        expr_parser_->Parse(local->defs.get());
        expr_parser_->Parse(local->body.get());
      }
      return local;
    }
    default:
      return shared_ptr<AbstractOzNode>(
          &OzNodeError::New()
          .SetNode(root)
          .SetError("Invalid local with too many 'in' separators"));
  }
}

shared_ptr<AbstractOzNode>
MidLevelScopeParser::ParseTry(shared_ptr<OzNodeGeneric>& root) {
  shared_ptr<OzNodeTry> try_node(new OzNodeTry);
  vector<int> edge_pos;
  SplitNodes(root->nodes, kOzSchema.try_branches, &edge_pos);
  if (edge_pos.size() == 0)
    return shared_ptr<AbstractOzNode>(
        &OzNodeError::New()
        .SetNode(root)
        .SetError(
            "Invalid try block, must have 'catch' or 'finally' sections"));

  OzNodeGeneric* body = OzNodeSlice(root->nodes, 0, edge_pos.front());
  const int last_pos = edge_pos.back();
  OzNodeGeneric* finally = nullptr;
  if (root->nodes[last_pos]->type == OzLexemType::FINALLY) {
    finally = OzNodeSlice(root->nodes, last_pos, root->nodes.size());
  }

  if (expr_parser_ != nullptr) {
    expr_parser_->Parse(body);
    if (finally != nullptr) expr_parser_->Parse(finally);
  }

  try_node->body.reset(body);
  // try_node->catches.reset(catches);
  try_node->finally.reset(finally);
  return try_node;
}

shared_ptr<AbstractOzNode>
MidLevelScopeParser::ParseIfBranch(shared_ptr<OzNodeGeneric>& root,
                                   bool pattern) {
  vector<int> edge_pos;
  SplitNodes(root->nodes, kOzSchema.cond_if_branches, &edge_pos);
  if (edge_pos.size() != 1)
    return shared_ptr<AbstractOzNode>(
        &OzNodeError::New()
        .SetNode(root)
        .SetError("Invalid conditional, must have exactly one 'then'"));

  const int then_pos = edge_pos[0];

  OzNodeGeneric* condition = OzNodeSlice(root->nodes, 0, then_pos);
  OzNodeGeneric* body =
      OzNodeSlice(root->nodes, then_pos + 1, root->nodes.size());
  if (expr_parser_ != nullptr) {
    expr_parser_->Parse(condition);
    expr_parser_->Parse(body);
  }

  if (pattern) {
    shared_ptr<OzNodePatternBranch> branch(new OzNodePatternBranch);
    branch->pattern.reset(condition);
    // TODO: Extract optional branch->condition
    branch->body.reset(body);
    return branch;
  } else {
    shared_ptr<OzNodeCondBranch> branch(new OzNodeCondBranch);
    branch->condition.reset(condition);
    branch->body.reset(body);
    return branch;
  }
}

shared_ptr<AbstractOzNode>
MidLevelScopeParser::ParseCaseBranch(shared_ptr<OzNodeGeneric>& root) {
  vector<int> edge_pos;
  SplitNodes(root->nodes, kOzSchema.cond_case_branches, &edge_pos);
  if (edge_pos.size() < 1)
    return shared_ptr<AbstractOzNode>(
        &OzNodeError::New()
        .SetNode(root)
        .SetError("Invalid pattern case, missing 'of'"));

  shared_ptr<OzNodePatternMatch> match(new OzNodePatternMatch);

  const int of_pos = edge_pos[0];
  OzNodeGeneric* value = OzNodeSlice(root->nodes, 0, of_pos);
  if (expr_parser_ != nullptr)
    expr_parser_->Parse(value);
  match->value.reset(value);

  int ibegin = of_pos + 1;
  for (uint i = 1; i <= edge_pos.size(); ++i) {
    const int iend = (i < edge_pos.size()
                      ? edge_pos[i]
                      : root->nodes.size());

    shared_ptr<OzNodeGeneric> branch(OzNodeSlice(root->nodes, ibegin, iend));
    match->branches.push_back(ParseIfBranch(branch, true));

    ibegin = iend + 1;
  }

  return match;
}

shared_ptr<AbstractOzNode>
MidLevelScopeParser::Parse(shared_ptr<OzNodeGeneric>& root) {
  VLOG(2) << __PRETTY_FUNCTION__ << " on root node:\n" << *root;

  switch (root->type) {
    case OzLexemType::BEGIN: {
      return ParseLocal(root);  // LOCAL or LPAREN
    }

    case OzLexemType::THREAD: {
      shared_ptr<OzNodeThread> thread(new OzNodeThread);
      thread->body = ParseLocal(root);
      return thread;
    }

    case OzLexemType::LOCK: {
      LOG(FATAL) << "Not implemented";
      break;
    }

    case OzLexemType::FUNCTOR: {
      shared_ptr<OzNodeFunctor> functor(new OzNodeFunctor);

      vector<int> edge_pos;
      SplitNodes(root->nodes, kOzSchema.functor_branches, &edge_pos);

      auto SetFunctorSection = [&](OzLexemType type, int ibegin, int iend) {
        shared_ptr<OzNodeGeneric> section(
            OzNodeSlice(root->nodes, ibegin, iend));
        if (expr_parser_ != nullptr) expr_parser_->Parse(section.get());

        switch (type) {
          case OzLexemType::FUNCTOR:
            // TODO: section must have exactly one child node - check & optimize
            functor->functor = section;
            break;
          case OzLexemType::EXPORT:
            functor->exports = section;
            break;
          case OzLexemType::IMPORT:
            functor->import = section;
            break;
          case OzLexemType::DEFINE:
            functor->define = section;
            break;
          case OzLexemType::REQUIRE:
            functor->require = section;
            break;
          case OzLexemType::PREPARE:
            functor->prepare = section;
            break;
          default:
            LOG(FATAL) << "Unexpected functor section type: " << type;
        }
      };

      OzLexemType type = OzLexemType::FUNCTOR;
      int ibegin = 0;
      for (uint iedge = 0; iedge < edge_pos.size(); ++iedge) {
        int iend = edge_pos[iedge];

        SetFunctorSection(type, ibegin, iend);

        type = root->nodes[iend]->type,
        ibegin = iend + 1;
      }
      SetFunctorSection(type, ibegin, root->nodes.size());
      return functor;
    }

    case OzLexemType::TRY: {
      return ParseTry(root);
    }

    case OzLexemType::RAISE: {
      return ParseLocal(root);
    }

    case OzLexemType::FOR: {
      LOG(FATAL) << "Not implemented";
      break;
    }

    case OzLexemType::IF:
    case OzLexemType::CASE: {
      shared_ptr<OzNodeCond> cond(new OzNodeCond);

      vector<int> edge_pos;
      SplitNodes(root->nodes, kOzSchema.cond_branches, &edge_pos);

      // Process else branch first:
      if (!edge_pos.empty()) {
        const int iedge = edge_pos.back();
        if (root->nodes[iedge]->type == OzLexemType::ELSE) {
          unique_ptr<OzNodeGeneric> else_branch(
              OzNodeSlice(root->nodes, iedge + 1, root->nodes.size()));
          if (expr_parser_ != nullptr)
            expr_parser_->Parse(else_branch.get());

          cond->else_branch.reset(else_branch.release());
          edge_pos.pop_back();
          root->nodes.erase(root->nodes.begin() + iedge, root->nodes.end());
        }
      }

      // Process conditional branches:
      int ibegin = 0;
      OzLexemType type = root->type;
      for (uint i = 0; i <= edge_pos.size(); ++i) {
        const uint iend = ((i < edge_pos.size())
                           ? edge_pos[i]
                           : root->nodes.size());

        shared_ptr<OzNodeGeneric> branch(
            OzNodeSlice(root->nodes, ibegin, iend));
        switch (type) {
          case OzLexemType::IF:
          case OzLexemType::ELSEIF: {
            cond->branches.push_back(ParseIfBranch(branch, false));
            break;
          }

          case OzLexemType::CASE:
          case OzLexemType::ELSECASE: {
            cond->branches.push_back(ParseCaseBranch(branch));
            break;
          }

          default:
            LOG(FATAL) << "Unexpected conditional section: " << type;
        }

        if (iend < root->nodes.size()) {
          ibegin = iend + 1;
          type = root->nodes[iend]->type;
        }
      }

      return cond;
    }

    case OzLexemType::FUN:
    case OzLexemType::PROC: {
      shared_ptr<OzNodeProc> proc(new OzNodeProc);
      if (root->nodes.size() < 2)
        return shared_ptr<AbstractOzNode>(
            &OzNodeError::New()
            .SetNode(root)
            .SetError("Invalid empty procedure declaration"));
      proc->signature = root->nodes[0];
      if (proc->signature->type != OzLexemType::CALL_BEGIN)
        return shared_ptr<AbstractOzNode>(
            &OzNodeError::New()
            .SetNode(root)
            .SetError("Invalid procedure signature"));
      root->nodes.erase(root->nodes.begin());
      proc->body = root;  // TODO: parse body as local
      proc->fun = (root->type == OzLexemType::FUN);
      return proc;
    }

    case OzLexemType::CLASS: {
      LOG(FATAL) << "Not implemented";
      break;
    }

    case OzLexemType::METH: {
      LOG(FATAL) << "Not implemented";
      break;
    }

    case OzLexemType::TOP_LEVEL:
    case OzLexemType::CALL_BEGIN:
    case OzLexemType::LIST_BEGIN:
    case OzLexemType::BEGIN_RECORD_FEATURES:
      expr_parser_->Parse(root.get());
      return root;

    default:
      LOG(FATAL) << "unhandled case " << root->type;
  }

  LOG(WARNING) << "not implemented: " << root->type;
  return root;
}

// -----------------------------------------------------------------------------

void ExpressionParser::Parse(OzNodeGeneric* const branch) {
  VLOG(2) << __PRETTY_FUNCTION__ << " on root node:\n" << *branch;
  ParseRecordCons(branch);

  // Unary operators are first
  ParseUnaryOperator(branch, OzLexemType::CELL_ACCESS);

  // TODO: Parse into OzNodeVar flags, without priorities
  ParseUnaryOperator(branch, OzLexemType::VAR_NODEF);
  ParseUnaryOperator(branch, OzLexemType::READ_ONLY);

  ParseUnaryOperator(branch, OzLexemType::NUMERIC_NEG);

  ParseBinaryOperatorRTL(branch, OzLexemType::RECORD_ACCESS);

  // Binary/N-ary operators: higher priorities are first.
  ParseBinaryOperatorLTR(branch, OzLexemType::NUMERIC_DIV);
  ParseNaryOperator(branch, OzLexemType::NUMERIC_MUL);

  ParseBinaryOperatorLTR(branch, OzLexemType::NUMERIC_MINUS);
  ParseNaryOperator(branch, OzLexemType::NUMERIC_ADD);

  ParseBinaryOperatorLTR(branch, OzLexemType::EQUAL);
  ParseBinaryOperatorLTR(branch, OzLexemType::DIFFERENT);
  ParseBinaryOperatorLTR(branch, OzLexemType::GREATER_OR_EQUAL);
  ParseBinaryOperatorLTR(branch, OzLexemType::LESS_OR_EQUAL);
  ParseBinaryOperatorLTR(branch, OzLexemType::GREATER_THAN);
  ParseBinaryOperatorLTR(branch, OzLexemType::LESS_THAN);

  ParseBinaryOperatorRTL(branch, OzLexemType::AND_THEN);
  ParseBinaryOperatorRTL(branch, OzLexemType::OR_ELSE);


  ParseNaryOperator(branch, OzLexemType::UNIFY);
  ParseBinaryOperatorLTR(branch, OzLexemType::CELL_ASSIGN);
  ParseBinaryOperatorLTR(branch, OzLexemType::ATTR_ASSIGN);

  ParseBinaryOperatorRTL(branch, OzLexemType::LIST_CONS);
  ParseNaryOperator(branch, OzLexemType::TUPLE_CONS);

  ParseBinaryOperatorLTR(branch, OzLexemType::RECORD_DEF_FEATURE);

  VLOG(2) << "Exiting " << __PRETTY_FUNCTION__
          << " with output node:\n" << *branch;
}

void ExpressionParser::ParseRecordCons(OzNodeGeneric* const branch) {
  VLOG(2) << __PRETTY_FUNCTION__ << " on root node:\n" << *branch;

  uint j = 0;
  for (uint i = 0; i < branch->nodes.size();) {
    const shared_ptr<AbstractOzNode>& node = branch->nodes[i];
    if (node->type == OzLexemType::RECORD_CONS) {
      CHECK_LT(i+2, branch->nodes.size());  // by design of RECORD_CONS

      shared_ptr<OzNodeRecord> record(new OzNodeRecord);
      record->label = branch->nodes[i+1];
      record->features =
          std::dynamic_pointer_cast<OzNodeGeneric>(branch->nodes[i+2]);
      CHECK_EQ(OzLexemType::BEGIN_RECORD_FEATURES, record->features->type);

      if (!record->features->nodes.empty()
          && record->features->nodes.back()->type == OzLexemType::RECORD_OPEN) {
        record->open = true;
        record->features->nodes.erase(record->features->nodes.end() - 1);
      } else {
        record->open = false;
      }

      record->tokens = OzLexemSlice(*node, *record->features);

      branch->nodes[j++] = record;
      i += 3;
    } else {
      branch->nodes[j++] = branch->nodes[i++];
    }
  }
  // Delete everything after j
  branch->nodes.erase(branch->nodes.begin() + j, branch->nodes.end());

  VLOG(2) << "Exiting " << __PRETTY_FUNCTION__
          << " with output node:\n" << *branch;
}

void ExpressionParser::ParseUnaryOperator(OzNodeGeneric* const branch,
                                          OzLexemType op_type) {
  int j = branch->nodes.size() - 1;
  for (int i = branch->nodes.size() - 2; i >= 0;) {
    const shared_ptr<AbstractOzNode>& node = branch->nodes[i];
    if (node->type == op_type) {
      shared_ptr<OzNodeUnaryOp> uop(new OzNodeUnaryOp);
      uop->type = op_type;
      uop->operation = node->tokens.first();
      uop->operand = branch->nodes[j];
      uop->tokens = OzLexemSlice(*node, *uop->operand);

      branch->nodes[j] = uop;
      i -= 1;
    } else {
      branch->nodes[--j] = branch->nodes[i];
      i -= 1;
    }
  }

  // Delete nodes[0 .. j[
  if (j >= 0)
    branch->nodes.erase(branch->nodes.begin(), branch->nodes.begin() + j);
}

void ExpressionParser::ParseBinaryOperatorLTR(OzNodeGeneric* const branch,
                                              OzLexemType op_type) {
  VLOG(3) << __PRETTY_FUNCTION__
          << " for operator " << op_type
          << " on node\n" << *branch;

  const int nnodes = branch->nodes.size();
  if (nnodes < 3) return;
  int i = 1;  // operator lexem
  int j = 0;  // left operand
  while (i <= nnodes - 2) {
    const shared_ptr<AbstractOzNode>& node = branch->nodes[i];
    if (node->type == op_type) {
      shared_ptr<OzNodeBinaryOp> bin_op(new OzNodeBinaryOp);
      bin_op->type = op_type;
      bin_op->operation = node->tokens.first();
      bin_op->lop = branch->nodes[j];
      bin_op->rop = branch->nodes[i+1];
      bin_op->tokens = OzLexemSlice(*bin_op->lop, *bin_op->rop);

      branch->nodes[j] = bin_op;
      i += 2;
    } else {
      branch->nodes[++j] = branch->nodes[i++];
    }
  }
  while (i < nnodes)
    branch->nodes[++j] = branch->nodes[i++];

  // Delete everything after j
  branch->nodes.erase(branch->nodes.begin() + j + 1, branch->nodes.end());
}

void ExpressionParser::ParseBinaryOperatorRTL(OzNodeGeneric* const branch,
                                              OzLexemType op_type) {
  VLOG(3) << __PRETTY_FUNCTION__
          << " for operator " << op_type
          << " on node\n" << *branch;

  const int nnodes = branch->nodes.size();
  if (nnodes < 3) return;
  int i = nnodes - 2;  // operator lexem
  int j = nnodes - 1;  // right operand
  while (i >= 1) {
    const shared_ptr<AbstractOzNode>& node = branch->nodes[i];
    if (node->type == op_type) {
      shared_ptr<OzNodeBinaryOp> bin_op(new OzNodeBinaryOp);
      bin_op->type = op_type;
      bin_op->operation = node->tokens.first();
      bin_op->lop = branch->nodes[i-1];
      bin_op->rop = branch->nodes[j];
      bin_op->tokens = OzLexemSlice(*bin_op->lop, *bin_op->rop);

      branch->nodes[j] = bin_op;
      i -= 2;
    } else {
      branch->nodes[--j] = branch->nodes[i--];
    }
  }
  while (i >= 0)
    branch->nodes[--j] = branch->nodes[i--];

  // Delete everything before j
  branch->nodes.erase(branch->nodes.begin(), branch->nodes.begin() + j);
}

void ExpressionParser::ParseNaryOperator(OzNodeGeneric* const branch,
                                         OzLexemType op_type) {
  uint i = 0, j = 0;
  while (i + 2 < branch->nodes.size()) {
    const shared_ptr<AbstractOzNode>& node = branch->nodes[i+1];
    if (node->type == op_type) {
      shared_ptr<OzNodeNaryOp> nary_op(new OzNodeNaryOp);
      nary_op->type = op_type;
      nary_op->operation = node->tokens.first();
      nary_op->operands.push_back(branch->nodes[i]);
      i += 1;

      while ((i + 1 < branch->nodes.size())
             && (branch->nodes[i]->type == op_type)) {
        nary_op->operands.push_back(branch->nodes[i+1]);
        i += 2;
      }

      nary_op->tokens = OzLexemSlice(*nary_op->operands.front(),
                                     *nary_op->operands.back());

      branch->nodes[j++] = nary_op;
    } else {
      branch->nodes[j++] = branch->nodes[i++];
    }
  }

  // Copy last elements
  while (i < branch->nodes.size())
    branch->nodes[j++] = branch->nodes[i++];

  // Delete everything after j
  branch->nodes.erase(branch->nodes.begin() + j, branch->nodes.end());
}

// -----------------------------------------------------------------------------

OzParser::OzParser() {
}

bool OzParser::Parse(const string& text) {
  ParsingResult<CharStream> lres = lexer_.Parse(text);
  if (lres.status != ParsingStatus::OK) {
    LOG(ERROR) << "Lex error: " << lres.errors[0];
    return false;
  }

  ParsingResult<OzLexemStream> pres = parser_.Parse(lexer_.lexems(), root_);
  if (pres.status != ParsingStatus::OK) {
    LOG(ERROR) << "Parse error: " << pres.errors[0];
    return false;
  }

  CheckErrorVisitor visitor;
  root_->AcceptVisitor(&visitor);
  if (!visitor.valid()) return false;

  return true;
}

ParsingResult<OzLexemStream>
OzParser::Parse(const OzLexemStream& input) {
  ParsingResult<OzLexemStream> result = parser_.Parse(input, root_);
  return result;
}

}}  // namespace combinators::oz