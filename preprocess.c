#include "punyc.h"

static int file_no;

// Returns the contents of a given file.
static char *read_file_string(char *path) {
  // By convention, read from stdin if a given filename is "-".
  FILE *fp = stdin;
  if (strcmp(path, "-")) {
    fp = fopen(path, "r");
    if (!fp)
      return NULL;
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
  printf(".file %d \"%s\"\n", ++file_no, path);
  return buf;
}

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
}

static Token *copy_token(Token *tok) {
  Token *t = malloc(sizeof(Token));
  *t = *tok;
  t->next = NULL;
  t->ty = NULL;
  return t;
}

// Append tok2 to the end fo tok1.
static Token *append(Token *tok1, Token *tok2) {
  if (!tok1 || tok1->kind == TK_EOF)
    return tok2;

  Token head = {};
  Token *cur = &head;

  for(; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next)
    cur = cur->next =copy_token(tok1);
  cur->next = tok2;
  return head.next;
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

    if (equal(tok, "include")) {
      tok = tok->next;

      if (tok->kind != TK_STR)
        error_tok(tok, "expected a filename");

      char *path = tok->contents;
      char *input = read_file_string(path);
      if(!input)
        error_tok(tok, "%s", strerror(errno));
      tok = append(tokenize(path, file_no, input), tok->next);
      continue;
    }

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
  if (!input)
    error("cannot open %s: %s", path, strerror(errno));
  Token *tok = tokenize(path, file_no, input);
  tok = preprocess(tok);
  convert_keywords(tok);
  return tok;
}