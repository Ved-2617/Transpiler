/*
 * ================================================================
 *  codegen.h  —  CODE GENERATORS
 *
 *  One generator per target language.  Each walks ir[] and writes
 *  idiomatic source text into a caller-supplied buffer.
 *
 *  All four functions follow the same signature:
 *    void gen<Lang>(char *buf, int sz)
 *      buf  — output buffer (zero-initialised by caller)
 *      sz   — total capacity of buf in bytes
 *
 *  Internal helpers (indentN, genCallExpr, genContainerDecl,
 *  genFieldDecl, emitLine*) are static in codegen.c and not
 *  exposed here.
 * ================================================================
 */
#ifndef CODEGEN_H
#define CODEGEN_H

/* Emit idiomatic Python source into buf. */
void genPython(char *buf, int sz);

/* Emit C89/C99 source into buf.
   Classes become typedef struct + free-function pairs.  */
void genC(char *buf, int sz);

/* Emit C++17 source into buf.
   Classes, templates, and STL containers are rendered natively. */
void genCpp(char *buf, int sz);

/* Emit Java 8+ source into buf.
   Wraps flat statements in `public class Output { public static void main … }`.
   Classes with an IR_MAIN_START block get their own main method. */
void genJava(char *buf, int sz);

#endif /* CODEGEN_H */
