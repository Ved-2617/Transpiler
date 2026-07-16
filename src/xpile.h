/*
 * ================================================================
 *  xpile.h  —  MASTER TYPE DEFINITIONS
 *
 *  Every module #includes this one file.  It contains:
 *    • Compiler-wide buffer-size constants (MAX_*)
 *    • TokenType enum  +  TOK_NAMES[]  (shared with lexer & parser)
 *    • IRType enum     +  IR_NAMES[]   (shared with parser & codegen)
 *    • Token, IRNode, ContainerInfo structs
 *
 *  Nothing in here is mutable at run-time.  No function prototypes
 *  live here — those belong in their own module headers.
 * ================================================================
 */
#ifndef XPILE_H
#define XPILE_H

/* ── BUFFER / LIMIT CONSTANTS ───────────────────────────────────── */
#define MAX_SOURCE       8192
#define MAX_TOKEN_LEN      80
#define MAX_TOKENS        900
#define MAX_IR_NODES      640
#define MAX_NAME_LEN      100   /* dest / src1 identifier-length fields  */
#define MAX_OP_LEN         24   /* operator / modifier-list field        */
#define MAX_TYPE_LEN       48   /* dtype field                           */
#define MAX_ERRORS         64
#define MAX_ERROR_LEN     220
#define MAX_OUTPUT      20000
#define MAX_CONTAINERS     64
#define MAX_ARG_LEN       200

/* ══════════════════════════════════════════════════════════════════
   TOKEN TYPES
   One universal set covers all four input languages.
   Language-specific keywords are resolved in lexer.c:identOrKeyword().
══════════════════════════════════════════════════════════════════ */
typedef enum {
    TOK_IDENT, TOK_NUMBER, TOK_STRING,
    TOK_ASSIGN,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_GT, TOK_LT, TOK_EQ, TOK_NEQ, TOK_GTE, TOK_LTE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_AMP, TOK_PIPE,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMICOLON, TOK_COLON,
    TOK_COMMA, TOK_DOT,
    TOK_ARROW, TOK_DCOLON,
    TOK_HASH,
    /* ── keywords ────────────────────────────────────────────────── */
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR,
    TOK_RETURN, TOK_PRINT,
    TOK_INT, TOK_FLOAT_KW, TOK_CHAR_KW, TOK_VOID, TOK_AUTO,
    TOK_BOOL, TOK_STRING_KW,
    TOK_PUBLIC, TOK_PRIVATE, TOK_PROTECTED,
    TOK_CLASS, TOK_STATIC, TOK_CONST, TOK_NEW,
    TOK_INCLUDE, TOK_USING, TOK_NAMESPACE,
    TOK_ENDL, TOK_CIN, TOK_COUT,
    TOK_TRUE, TOK_FALSE,
    /* ── meta ────────────────────────────────────────────────────── */
    TOK_NEWLINE, TOK_EOF, TOK_UNKNOWN
} TokenType;

/* Printable names, parallel to the enum above (used in JSON output). */
extern const char *TOK_NAMES[];

typedef struct {
    TokenType type;
    char      lexeme[MAX_TOKEN_LEN];
} Token;

/* ══════════════════════════════════════════════════════════════════
   INTERMEDIATE REPRESENTATION (IR)
   ALL four input languages compile to this universal IR.
   Code generators walk ir[] and emit per-language text.

   Field semantics per node type
   ──────────────────────────────
   IR_ASSIGN       dest = src1
   IR_BINOP        dest = src1 op src2
   IR_PRINT        print(dest)
   IR_IF           if (src1 op src2)
   IR_ELSE         else
   IR_WHILE        while (src1 op src2)
   IR_FOR          for dest=src1; dest op src2; dest++   dtype=loop-var-type
   IR_RETURN       return src1
   IR_BLOCK_START  {
   IR_BLOCK_END    }
   IR_CONTAINER_DECL  dest=varName  src1=elemType("K:V" for map)
                       op=kind(vector/list/map/set/array)  src2=ctorArgs
   IR_CALL         dest=object  src1=method  op=accessOp("."/"->")
                   src2=rawArgs  dtype=resultVar(or "")
   IR_FIELD_ACCESS dest=resultVar  src1=object  op=accessOp  src2=field
   IR_FIELD_SET    dest=object  src1=field  op=accessOp  src2=value
   IR_NEW          dest=resultVar  src1=className  src2=ctorArgs
   IR_CLASS_START  dest=className
   IR_CLASS_END    dest=className
   IR_FIELD_DECL   dest=fieldName  src1=initVal  op=modifiers  dtype=type
   IR_METHOD_START dest=methodName src1=returnType src2=params op=modifiers
   IR_METHOD_END   dest=methodName
   IR_CTOR_START   dest=className  src2=params  op=modifiers
   IR_CTOR_END     dest=className
   IR_MAIN_START   dest="main"    (Java: public static void main found)
   IR_MAIN_END     dest="main"
══════════════════════════════════════════════════════════════════ */
typedef enum {
    IR_ASSIGN,
    IR_BINOP,
    IR_PRINT,
    IR_IF,
    IR_ELSE,
    IR_WHILE,
    IR_FOR,
    IR_RETURN,
    IR_BLOCK_START,
    IR_BLOCK_END,
    IR_CONTAINER_DECL,
    IR_CALL,
    IR_FIELD_ACCESS,
    IR_FIELD_SET,
    IR_NEW,
    IR_CLASS_START,
    IR_CLASS_END,
    IR_FIELD_DECL,
    IR_METHOD_START,
    IR_METHOD_END,
    IR_CTOR_START,
    IR_CTOR_END,
    IR_MAIN_START,
    IR_MAIN_END,
    IR_UNKNOWN
} IRType;

extern const char *IR_NAMES[];

typedef struct {
    IRType type;
    char   dest [MAX_NAME_LEN];
    char   src1 [MAX_NAME_LEN];
    char   op   [MAX_OP_LEN];
    char   src2 [MAX_ARG_LEN];
    char   dtype[MAX_TYPE_LEN];
} IRNode;

/* ══════════════════════════════════════════════════════════════════
   CONTAINER METADATA
   Gathered by the parser so code generators can map .size(),
   .push_back() etc. to the correct per-language idiom.
══════════════════════════════════════════════════════════════════ */
typedef struct {
    char name    [MAX_NAME_LEN];
    char kind    [16];          /* vector / list / map / set / array    */
    char elemType[32];          /* "int" or "int:string" for map<K,V>   */
    char ctorArgs[40];          /* raw constructor args, e.g. "26,0"    */
} ContainerInfo;

#endif /* XPILE_H */
