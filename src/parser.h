/*
 * ================================================================
 *  parser.h  —  LANGUAGE PARSERS
 *
 *  Four front-end parsers (Python, C, C++, Java) each consume
 *  toks[] and emit nodes into ir[].  They share a single recursive-
 *  descent core (sharedStmt) so common constructs (if/while/for,
 *  assignments, containers, member calls, classes) are parsed once.
 *
 *  Call order:
 *    1. lexAll()        → fills toks[]
 *    2. one of the four parse*() functions below → fills ir[]
 *    3. codegen functions read ir[]
 * ================================================================
 */
#ifndef PARSER_H
#define PARSER_H

/*
 * Parse Python / "custom" pseudo-Python source.
 * Handles colon-terminated blocks, bare `print x` and `print(x)`,
 * and Python-style assignments with optional type hints.
 */
void parsePython(void);

/*
 * Parse C source.
 * Recognises a top-level `int main() { … }` wrapper and
 * strips it, so the IR contains only the body statements.
 * Also handles bare top-level variable/array declarations.
 */
void parseC(void);

/*
 * Parse C++ source.
 * Superset of parseC: additionally handles `class` declarations
 * (with public/private/protected sections, constructors, methods),
 * `using namespace`, template container types (vector<T>, map<K,V>…),
 * and `cout <<` output.
 */
void parseCpp(void);

/*
 * Parse Java source.
 * If the source contains a `class` keyword, the whole class is
 * parsed (fields, constructors, methods, and the special
 * `public static void main` entry point).
 * Falls back to bare-statement mode if no class is present.
 */
void parseJava(void);

#endif /* PARSER_H */
