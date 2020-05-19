#include "punyc.h"

static int align_to(int n, int align) {
  return (n + align - 1) & ~(align - 1);
}

int main(int argc, char **argv) {
  if (argc != 2) 
    error("%s: invalid number of arguments\n", argv[0]);

  // Tokenize and parse.
  Token *tok = tokenize(argv[1]);
  Program *prog = parse(tok);

  // Assign offsets to local variables.
  for (Function *fn = prog->fns; fn; fn = fn->next) {
    int offset = 32;
    for (Var *var = fn->locals; var; var = var->next) {
      offset += var->ty->size;
      var->offset = offset;
    }
    fn->stack_size = align_to(offset, 16);
  }

  // Traverse the AST to emit assembly.
  codegen(prog);

  return 0;
}