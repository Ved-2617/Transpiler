/*
 * ================================================================
 *  output.h  —  JSON SERIALIZER
 *
 *  outputJSON() is called after all four code generators have run.
 *  It writes a single JSON object to stdout that the Python server
 *  forwards verbatim to the browser:
 *
 *    {
 *      "tokens": [ { "type": "...", "lexeme": "..." }, … ],
 *      "ir":     [ { "type": "...", "dest": "...", … }, … ],
 *      "python": "…generated Python source…",
 *      "c":      "…generated C source…",
 *      "cpp":    "…generated C++ source…",
 *      "java":   "…generated Java source…",
 *      "srclang":"python"|"c"|"cpp"|"java"|"custom",
 *      "errors": [ "…", … ]
 *    }
 * ================================================================
 */
#ifndef OUTPUT_H
#define OUTPUT_H

/*
 * Runs all four code generators, then serialises tokens, IR nodes,
 * generated code, srclang, and errors to stdout as one JSON object.
 */
void outputJSON(void);

#endif /* OUTPUT_H */
