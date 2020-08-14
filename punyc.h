#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct Type Type;
typedef struct Member Member;
typedef struct GvarInitializer GvarInitializer;

//
// tokenize.c
//

// Token
typedef enum {
  TK_RESERVED, // Keyworeds or punctuators
  TK_IDENT,    // Identifiers
  TK_STR,      // String literals
  TK_NUM,      // Integer literals
  TK_EOF,      // End-of-file markers
} TokenKind;

// Token type
typedef struct Token Token;
struct Token {
  TokenKind kind; // Token kind
  Token *next;    // Next token
  long val;       // If kind is TK_NUM, its value
  Type *ty;       // Used if TK_NUM
  char *loc;      // Token location
  int len;        // Token length

  char *contents; // String literal contents including terminating '\0'
  char cont_len;  // string literal length

  char *filename; // Input filename
  char *input;    // Entire input string
  int lineno;     // Line number
  int file_no;    // File number for .loc directive
  bool at_bol;    // True if this token is at beginning of line
};

void error(char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void warn_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *s);
Token *skip(Token *tok, char *s);
bool consume(Token **rest, Token *tok, char *str);
void convert_keywords(Token *tok);
Token *tokenize(char *filename, int file_no, char *p);

//
// preprocess.c
//

Token *read_file(char *path);

//
// parse.c
//

// Variable
typedef struct Var Var;
struct Var {
  Var *next;
  char *name;    // Variable name
  Type *ty;      // Type
  Token *tok;    // representative token
  bool is_local; // local or global
  int align;     // alignment

  bool is_static;
  GvarInitializer *initializer;

  // Local variable
  int offset;

  // Gloval variable
  char *contents;
  int cont_len;
};

// AST node
typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_MOD,       // %
  ND_BITAND,    // &
  ND_BITOR,     // |
  ND_BITXOR,    // ^
  ND_SHL,       // <<
  ND_SHR,       // >>
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_COND,      // ?:
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member access)
  ND_ADDR,      // unary &
  ND_DEREF,     // unary *
  ND_NOT,       // !
  ND_BITNOT,    // ~
  ND_LOGAND,    // &&
  ND_LOGOR,     // ||
  ND_RETURN,    // "return"
  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_DO,        // "do"
  ND_SWITCH,    // "switch"
  ND_CASE,      // "case"
  ND_BLOCK,     // { ... }
  ND_BREAK,     // "break"
  ND_CONTINUE,  // "continue"
  ND_GOTO,      // "goto"
  ND_LABEL,     // Labeled statement
  ND_FUNCALL,   // Function call
  ND_EXPR_STMT, // Expression statement
  ND_STMT_EXPR, // Statement expression
  ND_VAR,       // Variable
  ND_NUM,       // Integer
  ND_CAST,      // Type cast
} NodeKind;

// AST node type
typedef struct Node Node;
struct Node {
  NodeKind kind; // Node kind
  Node *next;    // Next node
  Type *ty;      // Type, e.g. int or pointer to tin
  Token *tok;    // Representative token

  Node *lhs;     // Left-hand side
  Node *rhs;     // Right-hand side

  // "if" or "for" statement
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;

  // Assignment
  bool is_init;

  // Blcok or statement expression
  Node *body;

  // Struct member access
  Member *member;

  // Function call
  char *funcname;
  Type *func_ty;
  Node *args;

  // Goto or labeled statement
  char *label_name;

  // Switch-cases
  Node *case_next;
  Node *default_case;
  int case_label;
  int case_end_label;

  // Variable
  Var *var;

  // Integer literal
  long val;
};

// Global variable initializer. Global variables can be initialized
// either by a constatnt expression or a pointer to another global
// bariable with an addend.
struct GvarInitializer {
  GvarInitializer *next;
  int offset;

  // Scalar value
  int sz;
  long val;

  // Reference to another global variable
  char *label;
  long addend;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  Var *params;
  bool is_static;
  bool is_varargs;

  Node *node;
  Var *locals;
  int stack_size;
};

typedef struct {
  Var *globals;
  Function *fns;
} Program;

Node *new_cast(Node *expr, Type *ty);
long const_expr(Token **rest, Token *tok);
Program *parse(Token *tok);

//
// type.c
//

typedef enum {
  TY_VOID,
  TY_BOOL,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_ENUM,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_STRUCT,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;           // sizeof() value
  int align;          // alignment
  bool is_unsigned;   // unsigned or signed
  bool is_incomplete; // incomplete type
  bool is_const;

  // Pointer
  Type *base;

  // Declaration
  Token *name;
  Token *name_pos;

  // Array
  int array_len;

  // Struct
  Member *members;

  // Function type
  Type *return_ty;
  Type *params;
  bool is_varargs;
  Type *next;
};

// Struct member
struct Member {
  Member *next;
  Type *ty;
  Token *tok; // for error message
  Token *name;
  int align;
  int offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
int align_to(int n, int align);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
Type *struct_type(void);
int size_of(Type *ty);
Type *copy_type(Type *ty);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Program *prog);

//
// main.c
//

extern bool preprocess_only;