#include "combinators/oznode_compile_visitor.h"

#include "combinators/ozparser.h"

using namespace store;

namespace combinators { namespace oz {

store::Value Compile(const string& code, store::Store* store) {
  OzParser parser;
  CHECK(parser.Parse(code)) << "Error parsing: " << code;
  shared_ptr<OzNodeGeneric> root =
      std::dynamic_pointer_cast<OzNodeGeneric>(parser.root());
  LOG(INFO) << "AST:\n" << *root << std::endl;
  CHECK_EQ(OzLexemType::TOP_LEVEL, root->type);
  CompileVisitor visitor(store);
  store::Value result;
  for (shared_ptr<AbstractOzNode> node : root->nodes)
    result = visitor.Compile(node.get());
  return result;
}

// -----------------------------------------------------------------------------

// virtual
void CompileVisitor::Visit(OzNodeGeneric* node) {
  // Compiles the top-level procedure:

  if (node->type != OzLexemType::TOP_LEVEL) {
    LOG(FATAL) << "Cannot compile generic node: " << node;
  }

  result_.reset(new ExpressionResult);

  for (auto def : node->nodes) {
    def->AcceptVisitor(this);
  }
}

// virtual
void CompileVisitor::Visit(OzNode* node) {
  CHECK(!result_->statement());

  const OzLexem& lexem = node->tokens.first();
  switch (node->type) {
    case OzLexemType::INTEGER: {
      result_->SetValue(Operand(
          store::New::Integer(store_, boost::get<mpz_class>(lexem.value))));
      break;
    }
    case OzLexemType::ATOM: {
      result_->SetValue(Operand(
          store::New::Atom(store_, boost::get<string>(lexem.value))));
      break;
    }
    case OzLexemType::STRING: {
      result_->SetValue(Operand(
          store::New::String(store_, boost::get<string>(lexem.value))));
      break;
    }
    case OzLexemType::REAL: {
      result_->SetValue(Operand(
          store::New::Real(store_, boost::get<real::Real>(lexem.value))));
      break;
    }
    case OzLexemType::VAR_ANON: {
      LOG(FATAL) << "not implemented";  // Bytecode::NEW_VARIABLE
      break;
    }
    default:
      LOG(FATAL) << "Unexpected node: " << *node;
  }
}


// virtual
void CompileVisitor::Visit(OzNodeProc* node) {
  // proc {$ ...} means there is an expression result
  // proc {P ...} is a statement: P = proc {$ ...}
  shared_ptr<OzNodeCall> signature =
      std::dynamic_pointer_cast<OzNodeCall>(node->signature);
  shared_ptr<AbstractOzNode> param0 = signature->nodes[0];
  switch (param0->type) {
    case OzLexemType::VARIABLE: {
      CHECK(result_->statement());
      shared_ptr<OzNodeVar> proc_var =
          std::dynamic_pointer_cast<OzNodeVar>(param0);

      if (environment_->ExistsGlobally(proc_var->var_name)) {
        const Symbol& symbol = environment_->Get(proc_var->var_name);
      } else {
      }

      break;
    }
    case OzLexemType::VAR_ANON: {
      CHECK(!result_->statement());
      break;
    }
    default: LOG(FATAL) << "Invalid procedure: " << node;
  }

  // Create a new environment for this procedure,
  // using the current environment as root environment.
  NestedEnvironment nested_env(this);

  // Save the current state:
  shared_ptr<vector<Bytecode> > saved_segment = segment_;
  segment_.reset(new vector<Bytecode>());

  for (uint64 iparam = 1; iparam < signature->nodes.size(); ++iparam) {
    shared_ptr<OzNodeVar> param_var =
        std::dynamic_pointer_cast<OzNodeVar>(signature->nodes[iparam]);
    environment_->AddParameter(param_var->var_name);
  }

  if (node->fun) {
    // fun {F X Y} is equivalent to proc {F X Y Result}
    // TODO: add an invisible return parameter?
    LOG(FATAL) << "functions are not implemented";
  }
  node->body->AcceptVisitor(this);

  // Generate the Closure with the number of registers from the environment.
  const uint64 nparams = environment_->nparams();
  const uint64 nlocals = environment_->nlocals();
  const uint64 nclosures = environment_->nclosures();
  Closure* const closure =
      New::Closure(store_, segment_, nparams, nlocals, nclosures)
      .as<Closure>();

  LOG(INFO) << "Compiled procedure:\n" << closure->ToString();

  // Fills in the environment linking table.
  // const vector<string>&  names = environment_->closure_symbol_names();
  // env->insert(env->begin(), names.begin(), names.end());

  // Restore saved state:
  std::swap(segment_, saved_segment);

  if (result_->statement()) {
    // proc {P ...}, ie. P = proc {$ ...}
  } else {
    result_->SetValue(Operand(store::Optimize(closure).as<Closure>()));
  }
}

// virtual
void CompileVisitor::Visit(OzNodeVar* node) {
  if (result_->statement()) {
    LOG(FATAL) << "Invalid statement: " << node;
  }
  const Symbol& symbol = environment_->Get(node->var_name);
  result_->SetValue(symbol.GetOperand());
}

// virtual
void CompileVisitor::Visit(OzNodeRecord* node) {
  // store::Value label = Eval(node->label.get());
  // store::OpenRecord* record =
  //     store::New::OpenRecord(store_, label).as<store::OpenRecord>();

  // int64 auto_counter = 1;
  // for (auto feature : node->features->nodes) {
  //   store::Value feat_label;
  //   store::Value feat_value;

  //   if (feature->type == OzLexemType::RECORD_DEF_FEATURE) {
  //     OzNodeBinaryOp* def = dynamic_cast<OzNodeBinaryOp*>(feature.get());
  //     feat_label = Eval(def->lop.get());
  //     feat_value = Eval(def->rop.get());
  //   } else {
  //     feat_label = store::New::Integer(store_, auto_counter);
  //     feat_value = Eval(feature.get());
  //   }
  //   CHECK(record->Set(feat_label, feat_value));
  //   if (store::HasType(feat_label, store::Value::SMALL_INTEGER)
  //       && (store::IntValue(feat_label) == auto_counter))
  //     auto_counter++;
  // }
  // if (node->open)
  //   value_ = record;
  // else
  //   value_ = record->GetRecord(store_);
}


// virtual
void CompileVisitor::Visit(OzNodeBinaryOp* node) {
  // store::Value lop = Eval(node->lop.get());
  // store::Value rop = Eval(node->rop.get());

  // switch (node->type) {
  //   case OzLexemType::LIST_CONS: {
  //     value_ = store::New::List(store_, lop, rop);
  //     break;
  //   }
  //   default:
  //     LOG(FATAL) << "Binary operator not supported: " << node->type;
  // }
}

// virtual
void CompileVisitor::Visit(OzNodeUnaryOp* node) {
  // store::Value value = Eval(node->operand.get());
  // switch (node->type) {
  //   case OzLexemType::NUMERIC_NEG: {
  //     switch (value.type()) {
  //       case store::Value::SMALL_INTEGER: {
  //         value_ = store::New::Integer(store_, -store::IntValue(value));
  //         break;
  //       }
  //       default:
  //         LOG(FATAL) << "Unsupported operand: " << value.ToString();
  //     }
  //     break;
  //   }
  //   default:
  //     LOG(FATAL) << "Unary operator not supported: " << node->type;
  // }
}

// virtual
void CompileVisitor::Visit(OzNodeNaryOp* node) {
  // Operator may be:
  //  - unify
  //  - tuple
  //  - numeric multiply
  //  - numeric add
  const uint64 size = node->operands.size();
  CHECK_GE(size, 1UL);

  switch (node->operation.type) {
    case OzLexemType::UNIFY:
      break;
    case OzLexemType::TUPLE_CONS:
      // TODO:
      // case OzLexemType::TUPLE_CONS:
      //   value_ = store::New::Tuple(store_, size, operands);
      LOG(FATAL) << "not implemented";
      break;
    case OzLexemType::NUMERIC_MUL:
    case OzLexemType::NUMERIC_ADD:
      LOG(FATAL) << "not implemented";
      break;
    default:
      LOG(FATAL) << "invalid n-ary operator: " << node->operation;
  }

  // The result for the entire unification expression/statement:
  shared_ptr<ExpressionResult> result = result_;

  const bool is_statement = result->statement();
  if (is_statement) {
    // Create an expression result place-holder for the first expression.
    result_.reset(new ExpressionResult(environment_));
  } else {
    // First expression is the result for the entire unification expression.
  }

  // Compute and save the first operand.
  // Other operands will be unified against this one.
  // The first operand is the one being returned, if this is an expression.
  node->operands[0]->AcceptVisitor(this);
  shared_ptr<ExpressionResult> first = result_;

  for (uint i = 1; i < size; ++i) {
    // Compute the next operand:
    result_.reset(new ExpressionResult(environment_));
    node->operands[i]->AcceptVisitor(this);

    segment_->push_back(
        Bytecode(Bytecode::UNIFY,
                 first->value(),
                 result_->value()));
  }

  // Returns the result for this unify expression, if not a statement
  result_ = is_statement ? result : first;
}

// virtual
void CompileVisitor::Visit(OzNodeFunctor* node) {
  LOG(FATAL) << "Cannot evaluate functors";
}

// virtual
void CompileVisitor::Visit(OzNodeLocal* node) {
  unique_ptr<Environment::NestedLocalAllocator> local(
      environment_->NewNestedLocalAllocator());

  if (node->defs != nullptr) {
    node->defs->AcceptVisitor(this);
  }

  // Lock the local allocator:
  //  - symbols from this scope are still visible,
  //  - no new symbol may be defined in this scope.
  local->Lock();

  if (node->body != nullptr) {
    node->defs->AcceptVisitor(this);
  }

  // Remove the local symbols:
  local.reset();
}

// virtual
void CompileVisitor::Visit(OzNodeCond* node) {
  LOG(FATAL) << "Cannot evaluate conditionnals";
}

// virtual
void CompileVisitor::Visit(OzNodeCondBranch* node) {
  LOG(FATAL) << "Cannot evaluate branches";
}

// virtual
void CompileVisitor::Visit(OzNodePatternMatch* node) {
  LOG(FATAL) << "Cannot evaluate branches";
}

// virtual
void CompileVisitor::Visit(OzNodePatternBranch* node) {
  LOG(FATAL) << "Cannot evaluate branches";
}

// virtual
void CompileVisitor::Visit(OzNodeThread* node) {
  LOG(FATAL) << "Cannot evaluate threads";
}

// virtual
void CompileVisitor::Visit(OzNodeLoop* node) {
  LOG(FATAL) << "Cannot evaluate loops";
}

// virtual
void CompileVisitor::Visit(OzNodeForLoop* node) {
  LOG(FATAL) << "Cannot evaluate loops";
}

// virtual
void CompileVisitor::Visit(OzNodeLock* node) {
  LOG(FATAL) << "Cannot evaluate locks";
}

// virtual
void CompileVisitor::Visit(OzNodeTry* node) {
  LOG(FATAL) << "Cannot evaluate try blocks";
}

// virtual
void CompileVisitor::Visit(OzNodeRaise* node) {
  shared_ptr<ExpressionResult> result = result_;
  result_.reset(new ExpressionResult(environment_));
  node->exn->AcceptVisitor(this);
  segment_->push_back(
      Bytecode(Bytecode::EXN_RAISE, result_->value()));
  result_ = result;
  // No need to set the result value here.
}

// virtual
void CompileVisitor::Visit(OzNodeClass* node) {
  LOG(FATAL) << "classes are not implemented";
}

// virtual
void CompileVisitor::Visit(OzNodeSequence* node) {
  CHECK_GE(node->nodes.size(), 1);
  shared_ptr<ExpressionResult> result = result_;

  const uint64 ilast = (node->nodes.size() - 1);
  for (uint64 i = 0; i <= ilast; ++i) {
    const bool is_last = (i == ilast);
    if (is_last) {
      result_ = result;
    } else {
      result_.reset(new ExpressionResult());  // statement
    }
    node->nodes[i]->AcceptVisitor(this);
  }
}

// virtual
void CompileVisitor::Visit(OzNodeCall* node) {
  // Expression {Proc Param1 ... ParamK} implies an implicit return parameter.
  // Expression {Proc Param1 ... $ ... ParamK} has an explicit return parameter.

  shared_ptr<ExpressionResult> result = result_;
  const bool is_statement = result->statement();

  if (!is_statement) {
    result->SetupValuePlaceholder("CallReturnPlaceholder");
  }

  // Determine if there is a return parameter '$':
  bool has_var_anon = false;
  for (uint64 iparam = 1; iparam < node->nodes.size(); ++iparam) {
    if (node->nodes[iparam]->type == OzLexemType::VAR_ANON) {
      CHECK(!has_var_anon) << "Invalid call with multiple '$':\n" << *node;
      has_var_anon = true;
    }
  }

  // Return parameter requires this to be an expression:
  CHECK(!(has_var_anon && !is_statement))
      << "Invalid statement call with '$':\n" << *node;

  // Determine the actual number of parameters for the call,
  // including the implicit return value, if needed:
  uint64 nparams = (node->nodes.size() - 1);
  if (!is_statement && !has_var_anon) nparams += 1;

  ScopedTemp params_temp(environment_);
  Operand params_op;  // Invalid by default.

  if (nparams > 0) {
    params_op = params_temp.Allocate("CallParametersArray");

    // Create the parameters array:
    segment_->push_back(
        Bytecode(Bytecode::NEW_ARRAY,
                 params_op,
                 Operand(Value::Integer(nparams)),
                 Operand(KAtomEmpty())));  // dummy value

    // Compute each parameter and set its value in the array:
    for (uint64 iparam = 1; iparam < node->nodes.size(); ++iparam) {
      shared_ptr<AbstractOzNode> param = node->nodes[iparam];
      Operand param_op;
      result_.reset(new ExpressionResult(environment_));

      if (param->type == OzLexemType::VAR_ANON) {  // Explicit output parameter
        param_op = result->value();
        segment_->push_back(Bytecode(Bytecode::NEW_VARIABLE, param_op));

      } else {  // Normal parameter
        param->AcceptVisitor(this);
        param_op = result_->value();
      }

      CHECK(!param_op.invalid());
      segment_->push_back(
          Bytecode(Bytecode::ASSIGN_ARRAY,
                   params_op,
                   Operand(Value::Integer(iparam - 1)),
                   param_op));
    }

    // Initialize the implicit return-value parameter, if needed:
    if (!is_statement && !has_var_anon) {
      Operand param_op = result->value();
      segment_->push_back(Bytecode(Bytecode::NEW_VARIABLE, param_op));
      segment_->push_back(
          Bytecode(Bytecode::ASSIGN_ARRAY,
                   params_op,
                   Operand(Value::Integer(nparams - 1)),
                   param_op));
    }

  } else {
    // No parameter
  }

  // Evaluate the expression that determines which procedure to invoke:
  shared_ptr<AbstractOzNode> proc = node->nodes.front();
  result_.reset(new ExpressionResult(environment_));
  proc->AcceptVisitor(this);
  Operand proc_op = result_->value();

  // This is a little bit inaccurate, but good enough to start with:
  const bool native = (proc_op.type == Operand::IMMEDIATE)
      && (proc_op.value.IsA<Atom>());

  segment_->push_back(
      Bytecode(native ? Bytecode::CALL_NATIVE : Bytecode::CALL,
               proc_op,
               params_op));

  // Result for this call expression/statement:
  result_ = result;
}

// virtual
void CompileVisitor::Visit(OzNodeList* node) {
  LOG(FATAL) << "not implemented";
}


}}  // namespace combinators::oz