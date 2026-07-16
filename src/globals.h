/*
 * ================================================================
 *  globals.h  —  SHARED MUTABLE STATE
 *
 *  All modules that need to read or write the compiler's shared
 *  arrays (token stream, IR list, error list, container table,
 *  source-language tag) include this header.
 *
 *  The actual storage is defined exactly once in globals.c.
 * ================================================================
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#include "xpile.h"

/* ── Token stream (produced by the lexer) ───────────────────────── */
extern Token toks[MAX_TOKENS];
extern int   tokCount;

/* ── Intermediate Representation (produced by the parser) ────────── */
extern IRNode ir[MAX_IR_NODES];
extern int    irCount;

/* ── Error accumulator (written by any phase) ────────────────────── */
extern char errs[MAX_ERRORS][MAX_ERROR_LEN];
extern int  errCount;

/* ── Container/array metadata (gathered during parsing) ──────────── */
extern ContainerInfo containers[MAX_CONTAINERS];
extern int           containerCount;

/* ── Source language tag (set by main before parsing) ────────────── */
extern char srcLang[16];

/* ── Parser cursor (parser.c owns it; visible here for inline helpers) */
extern int pos;

#endif /* GLOBALS_H */
