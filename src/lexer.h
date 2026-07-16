/*
 * ================================================================
 *  lexer.h  —  UNIVERSAL LEXER
 *
 *  One lexer tokenises all four input languages (Python, C, C++,
 *  Java).  Language-specific keywords are resolved by a lookup
 *  table so the grammar layer never needs to re-identify them.
 *
 *  After lexAll() returns, globals toks[] and tokCount are ready
 *  for the parser.
 * ================================================================
 */
#ifndef LEXER_H
#define LEXER_H

#include "xpile.h"

/*
 * Map a raw identifier string to the correct TokenType keyword
 * constant, or TOK_IDENT if it is not a reserved word.
 * Called internally by lexAll(); exposed here so tests can use it.
 */
TokenType identOrKeyword(const char *word);

/*
 * Tokenise `src` (null-terminated C string of source code).
 * Fills globals toks[] and sets tokCount.
 * Unrecognised characters are reported via addErr() and skipped.
 */
void lexAll(const char *src);

#endif /* LEXER_H */
