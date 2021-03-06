#include "punyc.h"

static int top;
static int labelseq = 1;
static int brkseq;
static int contseq;
static char *argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *argreg16[] = {"di", "si", "dx", "cx", "r8w", "r9b"};
static char *argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static char *argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char *funcname;

static char *reg(int idx) {
  static char *r[] = {"r10", "r11", "r12", "r13", "r14", "r15"};
  if (idx < 0 || sizeof(r) / sizeof(*r) <= idx)
    error("registor out of range: %d", idx);
  return r[idx];
}

static char *regx(Type *ty, int idx) {
  if (ty->base || size_of(ty) == 8)
    return reg(idx);

  static char *r[] = {"r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
  if (idx < 0 || sizeof(r) / sizeof(*r) <= idx)
    error("ragesiter out of range: %d", idx);
  return r[idx];
}

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

// Pushes the given node's address to the stack.
static void gen_addr(Node *node) {
  switch (node->kind) {
    case ND_VAR:
      if (node->var->is_local)
        printf("  lea %s, [rbp-%d]\n", reg(top++), node->var->offset);
      else
        printf("  mov %s, offset %s\n", reg(top++), node->var->name);
      return;
    case ND_DEREF:
      gen_expr(node->lhs);
      return;
    case ND_MEMBER:
      gen_addr(node->lhs);
      printf("  add %s, %d\n", reg(top - 1), node->member->offset);
      return;
    case ND_COMMA:
      gen_expr(node->lhs);
      top--;
      gen_addr(node->rhs);
      return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value form where the stack top is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_FUNC)  {
    // If it is an array, do nothing because in general we can't load
    // an entire array to a register. As a result, the result of an
    // evaluation of an array becomes not the array itself but the
    // address of the array. In other words, this is where "array is
    // automatically covered to a pointer to the first element of
    // the array in C" occurs.
    return;
  }

  char *rs = reg(top -1);
  char *rd = regx(ty, top - 1);
  char *insn = ty->is_unsigned ? "movzx" : "movsx";

  // When we load a char or a short value to a register, we always
  // extend them to the size of int, so we can assume the lower half of
  // a register always contains a valid value. The upper half of a
  // register for char, short and int may contain garbage. When we load
  // a long value to a register, it simply occupies the entire register.
  int sz = size_of(ty);
  if (sz == 1)
    printf("  %s %s, byte ptr [%s]\n", insn, rd, rs);
  else if (sz == 2)
    printf("  %s %s, word ptr [%s]\n", insn, rd, rs);
  else if (sz == 4)
    printf("  mov %s, dword ptr [%s]\n", rd, rs);
  else
    printf("  mov %s, [%s]\n", rd, rs);
}

static void store(Type *ty) {
  char *rd = reg(top - 1);
  char *rs = reg(top - 2);
  int sz = size_of(ty);

  if (ty->kind == TY_STRUCT){
    for (int i = 0; i < ty->size; i++) {
      printf("  mov al, [%s+%d]\n", rs, i);
      printf("  mov [%s+%d], al\n", rd, i);
    }
  } else if (sz == 1){
    printf("  mov [%s], %sb\n", rd, rs);
  } else if (sz == 2) {
    printf("  mov [%s], %sw\n", rd, rs);
  } else if (sz == 4){
    printf("  mov [%s], %sd\n", rd, rs);
  } else {
    printf("  mov [%s], %s\n", rd, rs);
  }

  top--;
}

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  char *r = reg(top - 1);

  if (to->kind == TY_BOOL) {
    printf("  cmp %s, 0\n", r);
    printf("  setne %sb\n", r);
    printf("  movzx %s, %sb\n", r, r);
    return;
  }

  char *insn = to->is_unsigned ? "movzx" : "movsx";

  if (size_of(to) == 1) {
    printf("  %s %s, %sb\n", insn, r, r);
  } else if (size_of(to) == 2) {
    printf("  %s %s, %sw\n", insn, r, r);
  } else if (size_of(to) == 4) {
    printf("  mov %sd, %sd\n", r, r);
  } else if (!from->base && size_of(from) < 8 && !from->is_unsigned) {
    printf("  movsx %s, %sd\n", r, r);
  }
}

static void divmod(Node *node, char *rd, char *rs, char *r64, char *r32) {
    if (size_of(node->ty) == 8) {
      printf("  mov rax, %s\n", rd);
      if (node->ty->is_unsigned) {
        printf("  mov rdx, 0\n");
        printf("  div %s\n", rs);
      } else {
        printf("  cqo\n");
        printf("  idiv %s\n", rs);
      }
      printf("  mov %s, %s\n", rd, r64);
    } else {
      printf("  mov eax, %s\n", rd);
      if (node->ty->is_unsigned) {
        printf("  mov edx, 0\n");
        printf("  div %s\n", rs);
      } else {
        printf("  cdq\n");
        printf("  idiv %s\n", rs);
      }
      printf("  mov %s, %s\n", rd, r32);
    }
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  printf(".loc %d %d\n", node->tok->file_no, node->tok->lineno);

  switch (node->kind) {
    case ND_NUM:
      if (node->ty->kind == TY_LONG)
        printf("  movabs %s, %ld\n", reg(top++), node->val);
      else
        printf("  mov %s, %ld\n", reg(top++), node->val);
      return;
    case ND_VAR:
    case ND_MEMBER:
      gen_addr(node);
      load(node->ty);
      return;
    case ND_DEREF:
      gen_expr(node->lhs);
      load(node->ty);
      return;
    case ND_ADDR:
      gen_addr(node->lhs);
      return;
    case ND_ASSIGN:
      if (node->ty->kind == TY_ARRAY)
        error_tok(node->tok, "not an lvalue");
      if (node->lhs->ty->is_const && !node->is_init)
        error_tok(node->tok, "cannot assign to a const variable");

      gen_expr(node->rhs);
      gen_addr(node->lhs);
      store(node->ty);
      return;
    case ND_STMT_EXPR:
      for (Node *n = node->body; n; n = n->next)
        gen_stmt(n);
      top++;
      return;
    case ND_CAST:
      gen_expr(node->lhs);
      cast(node->lhs->ty, node->ty);
      return;
    case ND_COMMA:
      gen_expr(node->lhs);
      top--;
      gen_expr(node->rhs);
      return;
    case ND_COND: {
      int seq = labelseq++;
      gen_expr(node->cond);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je  .L.else.%d\n", seq);
      gen_expr(node->then);
      top--;
      printf("  jmp .L.end.%d\n", seq);
      printf(".L.else.%d:\n", seq);
      gen_expr(node->els);
      printf(".L.end.%d:\n", seq);
      return;
    }
    case ND_NOT:
      gen_expr(node->lhs);
      printf("  cmp %s, 0\n", reg(top - 1));
      printf("  sete %sb\n", reg(top - 1));
      printf("  movzx %s, %sb\n", reg(top - 1), reg(top - 1));
      return;
    case ND_BITNOT:
      gen_expr(node->lhs);
      printf("  not %s\n", reg(top - 1));
      return;
    case ND_LOGAND: {
      int seq = labelseq++;
      gen_expr(node->lhs);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je .L.false.%d\n", seq);
      gen_expr(node->rhs);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je .L.false.%d\n", seq);
      printf("  mov %s, 1\n", reg(top));
      printf("  jmp .L.end.%d\n", seq);
      printf(".L.false.%d:\n", seq);
      printf("  mov %s, 0\n", reg(top++));
      printf(".L.end.%d:\n", seq);
      return;
    }
    case ND_LOGOR: {
      int seq = labelseq++;
      gen_expr(node->lhs);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  jne .L.true.%d\n", seq);
      gen_expr(node->rhs);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  jne .L.true.%d\n", seq);
      printf("  mov %s, 0\n", reg(top));
      printf("  jmp .L.end.%d\n", seq);
      printf(".L.true.%d:\n", seq);
      printf("  mov %s, 1\n", reg(top++));
      printf(".L.end.%d:\n", seq);
      return;
    }
    case ND_FUNCALL: {
      if (node->lhs->kind == ND_VAR &&
          !strcmp(node->lhs->var->name, "__builtin_va_start")) {
        gen_expr(node->args);
        printf("  mov eax, [rbp-40]\n");
        printf("  mov [%s], eax\n", reg(top - 1));
        printf("  lea rax, [rbp-88]\n");
        printf("  mov [%s+16], rax\n", reg(top - 1));
        return;
      }
      // Save all temporary registers to the stack before evaluating
      // function arguments to allow each argument evaluation to use all
      // temporary registers. This is a workround for a register
      // exhaustion issue when evaluating a long expression containing
      // multiple function calls.
      int top_orig = top;
      top = 0;

      printf("  push r10\n");
      printf("  push r11\n");
      printf("  push r12\n");
      printf("  push r13\n");
      printf("  push r14\n");
      printf("  push r15\n");

      int nargs = 0;
      for (Node *arg = node->args; arg; arg = arg->next) {
        gen_expr(arg);
        printf("  push %s\n", reg(--top));
        printf("  sub rsp, 8\n");
        nargs++;
      }

      gen_expr(node->lhs);

      for (int i = nargs - 1; i >= 0; i--) {
        printf("  add rsp, 8\n");
        printf("  pop %s\n", argreg64[i]);
      }

      printf("  mov rax, 0\n");
      printf("  call %s\n", reg(--top));

      // The System V x86-64 ABI has a special rule ragarding a boolean
      // return value that only the lower 8 bits are valid for it and
      // the upper 56 bits may contain garbage. Here, we claer the upper
      // 56 bits.
      if (node->ty->kind == TY_BOOL)
        printf("  movzx eax, al\n");

      top = top_orig;
      printf("  pop r15\n");
      printf("  pop r14\n");
      printf("  pop r13\n");
      printf("  pop r12\n");
      printf("  pop r11\n");
      printf("  pop r10\n");

      printf("  mov %s, rax\n", reg(top++));
      return;
  }
  }

  // Binary expressions
  gen_expr(node->lhs);
  gen_expr(node->rhs);

  char *rd = regx(node->lhs->ty, top - 2);
  char *rs = regx(node->lhs->ty, top - 1);
  top--;

  switch (node->kind) {
  case ND_ADD:
    printf("  add %s, %s\n", rd, rs);
    return;
  case ND_SUB:
    printf("  sub %s, %s\n", rd, rs);
    return;
  case ND_MUL:
    printf("  imul %s, %s\n", rd, rs);
    return;
  case ND_DIV:
    divmod(node, rd, rs, "rax", "eax");
    return;
  case ND_MOD:
    divmod(node, rd, rs, "rdx", "edx");
    return;
  case ND_BITAND:
    printf("  and %s, %s\n", rd, rs);
    return;
  case ND_BITOR:
    printf("  or %s, %s\n", rd, rs);
    return;
  case ND_BITXOR:
    printf("  xor %s, %s\n", rd, rs);
    return;
  case ND_EQ:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  sete al\n");
    printf("  movzb %s, al\n", rd);
    return;
  case ND_NE:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  setne al\n");
    printf("  movzb %s, al\n", rd);
    return;
  case ND_LT:
    printf("  cmp %s, %s\n", rd, rs);
    if (node->lhs->ty->is_unsigned)
      printf("  setb al\n");
    else
      printf("  setl al\n");
    printf("  movzb %s, al\n", rd);
    return;
  case ND_LE:
    printf("  cmp %s, %s\n", rd, rs);
    if (node->lhs->ty->is_unsigned)
      printf("  setbe al\n");
    else
      printf("  setle al\n");
    printf("  movzb %s, al\n", rd);
    return;
  case ND_SHL:
    printf("  mov rcx, %s\n", reg(top));
    printf("  shl %s, cl\n", rd);
    return;
  case ND_SHR:
    printf("  mov rcx, %s\n", reg(top));
    if (node->lhs->ty->is_unsigned)
      printf("  shr %s, cl\n", rd);
    else
      printf("  sar %s, cl\n", rd);
    return;
  default:
    error_tok(node->tok, "invalid expression");
  }
}

static void gen_stmt(Node *node) {
  printf(".loc %d %d\n", node->tok->file_no, node->tok->lineno);

  switch (node->kind) {
  case ND_IF: {
    int seq = labelseq++;
    if (node->els) {
      gen_expr(node->cond);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je .L.else.%d\n", seq);
      gen_stmt(node->then);
      printf("  jmp .L.end.%d\n", seq);
      printf(".L.else.%d:\n", seq);
      gen_stmt(node->els);
      printf(".L.end.%d:\n", seq);
    } else {
      gen_expr(node->cond);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je .L.end.%d\n", seq);
      gen_stmt(node->then);
      printf(".L.end.%d:\n", seq);
    }
    return;
  }
  case ND_FOR: {
    int seq = labelseq++;
    int brk = brkseq;
    int cont = contseq;
    brkseq = contseq = seq;

    if (node->init)
      gen_stmt(node->init);
    printf(".L.begin.%d:\n", seq);
    if (node->cond) {
      gen_expr(node->cond);
      printf("  cmp %s, 0\n", reg(--top));
      printf("  je .L.break.%d\n", seq);
    }
    gen_stmt(node->then);
    printf(".L.continue.%d:\n", seq);
    if(node->inc)
      gen_stmt(node->inc);
    printf("  jmp .L.begin.%d\n", seq);
    printf(".L.break.%d:\n", seq);

    brkseq = brk;
    contseq = cont;
    return;
  }
  case ND_DO: {
    int seq = labelseq++;
    int brk = brkseq;
    int count = contseq;
    brkseq = contseq = seq;

    printf(".L.begin.%d:\n", seq);
    gen_stmt(node->then);
    printf(".L.continue.%d:\n", seq);
    gen_expr(node->cond);
    printf("  cmp %s, 0\n", reg(--top));
    printf("  jne .L.begin.%d\n", seq);
    printf(".L.break.%d:\n", seq);

    brkseq = brk;
    contseq = count;
    return;
  }
  case ND_SWITCH: {
    int seq = labelseq++;
    int brk = brkseq;
    brkseq = seq;
    node->case_label = seq;

    gen_expr(node->cond);

    for (Node *n = node->case_next; n; n = n->case_next) {
      n->case_label = labelseq++;
      n->case_end_label = seq;
      printf("  cmp %s, %ld\n", reg(top - 1), n->val);
      printf("  je .L.case.%d\n", n->case_label);
    }
    top--;

    if (node->default_case) {
      int i = labelseq++;
      node->default_case->case_end_label = seq;
      node->default_case->case_label = i;
      printf("  jmp .L.case.%d\n", i);
    }

    printf("  jmp .L.break.%d\n", seq);
    gen_stmt(node->then);
    printf(".L.break.%d:\n", seq);

    brkseq = brk;
    return;
  }
  case ND_CASE:
    printf(".L.case.%d:\n", node->case_label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_BREAK:
    if (brkseq == 0)
      error_tok(node->tok, "stray break");
    printf("  jmp .L.break.%d\n", brkseq);
    return;
  case ND_CONTINUE:
    if (contseq == 0)
      error_tok(node->tok, "stray continue");
    printf("  jmp .L.continue.%d\n", contseq);
    return;
  case ND_GOTO:
    printf("  jmp .L.label.%s.%s\n", funcname, node->label_name);
    return;
  case ND_LABEL:
    printf(".L.label.%s.%s:\n", funcname, node->label_name);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      printf("  mov rax, %s\n", reg(--top));
    }
    printf("  jmp .L.return.%s\n", funcname);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    top--;
    return;
  default:
    error_tok(node->tok, "invalid statement");
  }
}

static void emit_data(Program *prog) {
  printf(".bss\n");

  for (Var *var = prog->globals; var; var = var->next) {
    if (var->initializer)
      continue;

    printf(".align %d\n", var->align);
    if (!var->is_static)
      printf(".globl %s\n", var->name);
    printf("%s:\n", var->name);
    printf("  .zero %d\n", size_of(var->ty));
  }

  printf(".data\n");

  for (Var *var = prog->globals; var; var = var->next) {
    if (!var->initializer)
      continue;

    printf(".align %d\n", var->align);
    if (!var->is_static)
      printf(".globl %s\n", var->name);
    printf("%s:\n", var->name);

    int offset = 0;
    for (GvarInitializer *init = var->initializer; init; init = init->next) {
      if (offset < init->offset)
        printf("  .zero %d\n", init->offset - offset);
      offset = init->offset + init->sz;

      if (init->label)
        printf("  .quad %s%+ld\n", init->label, init->addend);
      else if (init->sz == 1)
        printf("  .byte %ld\n", init->val);
      else if (init->sz == 2)
        printf("  .short %ld\n", init->val);
      else if (init->sz == 4)
        printf("  .long %ld\n", init->val);
      else
        printf("  .quad %ld\n", init->val);
    }

    if (offset < size_of(var->ty))
      printf("  .zero %d\n", size_of(var->ty) - offset);
  }
}

static char *get_argreg(int sz, int idx) {
  if (sz == 1)
    return argreg8[idx];
  if (sz == 2)
    return argreg16[idx];
  if (sz == 4)
    return argreg32[idx];
  assert(sz == 8);
  return argreg64[idx];
}

static void emit_text(Program *prog) {
  printf(".text\n");

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    if (!fn->is_static)
      printf(".globl %s\n", fn->name);
    printf("%s:\n", fn->name);
    funcname = fn->name;

    //Prologue. r12-r15 are callee-saved registers.
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", fn->stack_size);
    printf("  mov [rbp-8], r12\n");
    printf("  mov [rbp-16], r13\n");
    printf("  mov [rbp-24], r14\n");
    printf("  mov [rbp-32], r15\n");

    // Save arg registers if funtion is variadic
    if (fn->is_varargs) {
      int n = 0;
      for (Var *var = fn->params; var; var = var->next)
        n++;

      printf("  mov [rbp-88], rdi\n");
      printf("  mov [rbp-80], rsi\n");
      printf("  mov [rbp-72], rdx\n");
      printf("  mov [rbp-64], rcx\n");
      printf("  mov [rbp-56], r8\n");
      printf("  mov [rbp-48], r9\n");
      printf("  mov dword ptr [rbp-40], %d\n", n * 8);
    }

    // Push arguments to the stack
    int i = 0;
    for (Var *var = fn->params; var; var = var->next)
      i++;

    for (Var *var = fn->params; var; var = var->next) {
      char *r = get_argreg(size_of(var->ty), --i);
      printf("  mov [rbp-%d], %s\n", var->offset, r);
    }

    // Emit code
    for (Node *n = fn->node; n; n = n->next) {
      gen_stmt(n);
      assert(top == 0);
    }

    // Epilogue
    printf(".L.return.%s:\n", funcname);
    printf("  mov r12, [rbp-8]\n");
    printf("  mov r13, [rbp-16]\n");
    printf("  mov r14, [rbp-24]\n");
    printf("  mov r15, [rbp-32]\n");
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");
    printf("  ret\n");
  }
}

void codegen(Program *prog) {
  printf(".intel_syntax noprefix\n");
  emit_data(prog);
  emit_text(prog);
}