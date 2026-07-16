/*
 * ================================================================
 *  globals.c  —  GLOBAL DATA DEFINITIONS
 *
 *  Defines every array / variable that is declared extern in
 *  globals.h, plus the two read-only name tables (TOK_NAMES and
 *  IR_NAMES) that are declared extern in xpile.h.
 *
 *  No logic lives here — this is pure data.
 * ================================================================
 */
#include "xpile.h"
#include "globals.h"

/* ── Read-only name tables (parallel to the enums in xpile.h) ────── */

const char *TOK_NAMES[] = {
    "IDENT","NUMBER","STRING",
    "ASSIGN","PLUS","MINUS","STAR","SLASH","PERCENT",
    "GT","LT","EQ","NEQ","GTE","LTE","AND","OR","NOT",
    "AMP","PIPE","LSHIFT","RSHIFT",
    "LPAREN","RPAREN","LBRACE","RBRACE","LBRACKET","RBRACKET",
    "SEMICOLON","COLON","COMMA","DOT","ARROW","DCOLON","HASH",
    "IF","ELSE","WHILE","FOR",
    "RETURN","PRINT",
    "INT","FLOAT_KW","CHAR_KW","VOID","AUTO",
    "BOOL","STRING_KW",
    "PUBLIC","PRIVATE","PROTECTED",
    "CLASS","STATIC","CONST","NEW",
    "INCLUDE","USING","NAMESPACE","ENDL","CIN","COUT",
    "TRUE","FALSE",
    "NEWLINE","EOF","UNKNOWN"
};

const char *IR_NAMES[] = {
    "IR_ASSIGN","IR_BINOP","IR_PRINT",
    "IR_IF","IR_ELSE","IR_WHILE","IR_FOR","IR_RETURN",
    "IR_BLOCK_START","IR_BLOCK_END",
    "IR_CONTAINER_DECL","IR_CALL","IR_FIELD_ACCESS","IR_FIELD_SET","IR_NEW",
    "IR_CLASS_START","IR_CLASS_END","IR_FIELD_DECL",
    "IR_METHOD_START","IR_METHOD_END","IR_CTOR_START","IR_CTOR_END",
    "IR_MAIN_START","IR_MAIN_END",
    "IR_UNKNOWN"
};

/* ── Mutable compiler state ─────────────────────────────────────── */

Token toks[MAX_TOKENS];
int   tokCount = 0;

IRNode ir[MAX_IR_NODES];
int    irCount = 0;

char errs[MAX_ERRORS][MAX_ERROR_LEN];
int  errCount = 0;

ContainerInfo containers[MAX_CONTAINERS];
int           containerCount = 0;

char srcLang[16] = "custom";

int pos = 0;   /* parser cursor into toks[] */
