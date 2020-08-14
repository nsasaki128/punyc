#include "punyc.h"

// Returns the contents of a given file.
static char *read_file_string(char *path) {
  // By convention, read from stdin if a given filename is "-".
  FILE *fp = stdin;
  if (strcmp(path, "-")) {
    fp = fopen(path, "r");
    if (!fp)
      error("cannot open %s: %s", path, strerror(errno));
  }

  int buflen = 4096;
  int nread = 0;
  char *buf = malloc(buflen);

  // Read the entire file.
  for (;;) {
    int end = buflen - 2; // extra 2 bytes for the trailing "\n\0"
    int n = fread(buf + nread, 1, end - nread, fp);
    if (n == 0)
      break;
    nread += n;
    if (nread == end) {
      buflen *= 2;
      buf = realloc(buf, buflen);
    }
  }

  if (fp != stdin)
    fclose(fp);

  // Canonicalize the last line by appending "\n"
  // if it does not end with a newline.
  if (nread == 0 || buf[nread - 1] != '\n')
    buf[nread++] = '\n';
  buf[nread] = '\0';

  // Emit a .file directive for the assembler.
  printf(".file 1 \"%s\"\n", path);
  return buf;
}

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess(Token *tok) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // Pass through if it is not a "#".
    if (!is_hash(tok)) {
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }

    tok = tok->next;

    // `#`-only line is legal. It's called a null directive.
    if (tok->at_bol)
      continue;

    error_tok(tok, "invalid preprocessor directive");
  }

  cur->next = tok;
  return head.next;
}

// Entry point function of the preprocessor.
Token *read_file(char *path) {
  char *input = read_file_string(path);
  Token *tok = tokenize(path, input);
  tok = preprocess(tok);
  convert_keywords(tok);
  return tok;
}