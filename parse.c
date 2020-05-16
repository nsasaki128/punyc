// Thid file contains a recursice descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.
//
// Input tokens are represented by a linked list. Unlike many recursive
// descent partterns, we don't have the notion of the "input token stream".
// Most parsing functions don't change the global state of the pareser.
// So it is very easy to lookahead arbitarry number of tokens in this
// parser.

#include "punyc.h"

// All local variable instances created during parsing are
// accumulated to this list.
Var *locals;

static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);

// Find a local variable by name.
static Var *find_var(Token *tok) {
  for (Var *var = locals; var; var=var->next)
    if (strlen(var->name) == tok->len && !strncmp(tok->loc, var->name, tok->len))
      return var;
  return NULL;
}

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->tok = tok;
  return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

static Node *new_num(long val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_var_node(Var *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

static Var *new_lvar(char *name, Type *ty) {
  Var *var = calloc(1, sizeof(Var));
  var->name = name;
  var->ty = ty;
  var->next = locals;
  locals = var;
  return var;
}

static char *get_ident(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected an identifier");
  return strndup(tok->loc, tok->len);
}

static long get_number(Token *tok) {
  if (tok->kind != TK_NUM)
    error_tok(tok, "expected a number");
  return tok->val;
}

// typespec = "int"
static Type *typespec(Token **rest, Token *tok) {
  *rest = skip(tok, "int");
  return ty_int;
}

// declarator =  "*"* ident
static Type *declarator(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*"))
    ty = pointer_to(ty);
  
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected a cariable name");

  ty->name = tok;
  *rest = tok->next;
  return ty;
}

// declaration = typespec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)?";"
static Node *declaration(Token **rest, Token *tok) {
  Type *basety = typespec(&tok, tok);

  Node head = {};
  Node *cur = &head;
  int cnt = 0;

  while (!equal(tok, ";")) {
    if (cnt++ > 0)
      tok = skip(tok, ",");
    
    Type *ty = declarator(&tok, tok, basety);
    Var *var = new_lvar(get_ident(ty->name), ty);

    if (!equal(tok, "="))
      continue;
    
    Node *lhs = new_var_node(var, ty->name);
    Node *rhs = assign(&tok, tok->next);
    Node *node = new_binary(ND_ASSIGN, lhs, rhs, tok);
    cur = cur->next = new_unary(ND_EXPR_STMT, node, tok);
  }

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  *rest = tok->next;
  return node;
}

// stmt = "return" expr ";"
//      | "if" + "(" expr ")" stmt ("else" stmt)?
//      | "for" + "(" expr? ";" expr? ";" expr? ")" stmt 
//      | "{" compund-stmt
//      | expr ";"
static Node *stmt(Token **rest, Token *tok) {

  if (equal(tok, "return")) {
    Node *node = new_node(ND_RETURN, tok);
    node->lhs = expr(&tok, tok->next);
    *rest = skip(tok, ";");
    return node;
  }

  if (equal(tok, "if")) {
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);
    if (equal(tok, "else"))
      node->els = stmt(&tok, tok->next);
    *rest = tok;
    return node;
  }

  if (equal(tok, "for")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");

    if(!equal(tok, ";"))
      node->init = expr_stmt(&tok, tok);
    tok = skip(tok, ";");

    if(!equal(tok, ";"))
      node->cond = expr(&tok, tok);
    tok = skip(tok, ";");

    if(!equal(tok, ")"))
      node->inc = expr_stmt(&tok, tok);
    tok = skip(tok, ")");

    node->then = stmt(rest, tok);
    return node;
  }

  if (equal(tok, "while")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(rest, tok);
    return node;
  }
  
  if (equal(tok, "{"))
    return compound_stmt(rest, tok->next);

  Node *node = expr_stmt(&tok, tok);
  *rest = skip(tok, ";");
  return node;
}

// compund-stmt = (declaration | stmt)* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_BLOCK, tok);

  Node head = {};
  Node *cur = &head;
  while (!equal(tok, "}")) {
    if (equal(tok, "int"))
      cur = cur->next = declaration(&tok, tok);
    else
      cur = cur->next = stmt(&tok, tok);
    add_type(cur);
  }
  
  node->body = head.next;
  *rest = tok->next;
  return node;
}

// expr-stmt = expr
static Node *expr_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_EXPR_STMT, tok);
  node->lhs = expr(rest, tok);
  return node;
}

// expr = assign
static Node *expr(Token **rest, Token *tok) {
  return assign(rest, tok);
}

// assign = equality ("=" assign)?
static Node *assign(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);

  if (equal(tok, "="))
    return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

  *rest = tok;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);
  
  for (;;) {
    if (equal(tok, "==")) {
      node = new_binary(ND_EQ, node, NULL, tok);
      node->rhs = relational(&tok, tok->next);
      continue;
    }

    if (equal(tok, "!=")) {
      node = new_binary(ND_NE, node, NULL, tok);
      node->rhs = relational(&tok, tok->next);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);

  for (;;) {
    if (equal(tok, "<")) {
      node = new_binary(ND_LT, node, NULL, tok);
      node->rhs = add(&tok, tok->next);
      continue;
    }

    if (equal(tok, "<=")) {
      node = new_binary(ND_LE, node, NULL, tok);
      node->rhs = add(&tok, tok->next);
      continue;
    }

    if (equal(tok, ">")) {
      node = new_binary(ND_LT, NULL, node, tok);
      node->lhs = add(&tok, tok->next);
      continue;
    }

    if (equal(tok, ">=")) {
      node = new_binary(ND_LE, NULL, node, tok);
      node->lhs = add(&tok, tok->next);
      continue;
    }

    *rest = tok;
    return node;
  }
}


// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a point, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This fucntion takes care of the scaling.
static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);

  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "invalid operands");

  // Canonicalize `num + ptr` to `ptr + num`.
  if (!lhs->ty->base && rhs->ty->base){
    Node *tmp = lhs;
    lhs = rhs;
    rhs =tmp;
  }

  // ptr + num
  rhs = new_binary(ND_MUL, rhs, new_num(8, tok), tok);
  return new_binary(ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num - num
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);

  // ptr - num
  if (lhs->ty->base && is_integer(rhs->ty)) {
    rhs = new_binary(ND_MUL, rhs, new_num(8, tok), tok);
    return new_binary(ND_SUB, lhs, rhs, tok);
  }

  // ptr - ptr, which returns how many elements are between the two.
  if (lhs->ty->base && rhs->ty->base) {
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    return new_binary(ND_DIV, node, new_num(8, tok), tok);
  }

  error_tok(tok, "invalid operands");
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);

  for (;;) {
    Token *start = tok;
    if (equal(tok, "+")) {
      node = new_add(node, mul(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "-")) {
      node = new_sub(node, mul(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}


// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = unary(&tok, tok);
  
  for (;;) {
    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, NULL, tok);
      node->rhs = unary(&tok, tok->next);
      continue;
    }

    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, NULL, tok);
      node->rhs = unary(&tok, tok->next);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// unary = ("+" | "-") unary
//       | primary
static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return unary(rest, tok->next);

  if (equal(tok, "-"))
    return new_binary(ND_SUB, new_num(0, tok), unary(rest, tok->next), tok);

  if (equal(tok, "&"))
    return new_unary(ND_ADDR, unary(rest, tok->next), tok);

  if (equal(tok, "*"))
    return new_unary(ND_DEREF, unary(rest, tok->next), tok);

  return primary(rest, tok);
}

// func-args = "(" (assing ("," assing)*?) ")"
static Node *func_args(Token **rest, Token *tok) {
  Node head = {};
  Node *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");
    cur = cur->next = assign(&tok, tok);
  }

  *rest = skip(tok, ")");
  return head.next;
}

// primary = "(" expr ")" | ident func-args? | num
// args = "(" ")"
static Node *primary(Token **rest, Token *tok) {
  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }

  if (tok->kind == TK_IDENT) {
    // Function call
    if (equal(tok->next, "(")) {
      Node *node = new_node(ND_FUNCALL, tok);
      node->funcname = strndup(tok->loc, tok->len);
      node->args = func_args(rest, tok->next->next);
      return node;
    }

    // Variable
    Var *var = find_var(tok);
    if (!var)
      error_tok(tok, "undefined variable");
    *rest = tok->next;
    return new_var_node(var, tok);
  }
  Node *node = new_num(get_number(tok), tok);
  *rest = tok->next;
  return node;
}

// program = stmt*
Function *parse(Token *tok) {
  tok = skip(tok, "{");

  Function *prog = calloc(1, sizeof(Function));
  prog->node = compound_stmt(&tok, tok)->body;
  prog->locals = locals;
  return prog;
}