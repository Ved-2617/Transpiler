/*
 * ================================================================
 *  output.c  —  JSON SERIALIZER IMPLEMENTATION
 *
 *  Calls all four code generators, then writes one JSON object to
 *  stdout.  No logic other than serialisation lives here.
 *
 *  Character escaping is handled by util.c:jsonEsc() so embedded
 *  quotes, backslashes, and newlines inside generated code strings
 *  are always valid JSON.
 * ================================================================
 */
#include <stdio.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"
#include "codegen.h"
#include "output.h"

void outputJSON(void) {
    /* ── Run all four generators ─────────────────────────────────── */
    static char py[MAX_OUTPUT], cb[MAX_OUTPUT];
    static char cp[MAX_OUTPUT], jv[MAX_OUTPUT];

    genPython(py, MAX_OUTPUT);
    genC     (cb, MAX_OUTPUT);
    genCpp   (cp, MAX_OUTPUT);
    genJava  (jv, MAX_OUTPUT);

    /* Shared escape buffer — large enough for the biggest output. */
    static char esc[MAX_OUTPUT * 2];

    printf("{");

    /* ── tokens array ─────────────────────────────────────────────── */
    printf("\"tokens\":[");
    int first = 1;
    for (int i = 0; i < tokCount; i++) {
        if (toks[i].type==TOK_NEWLINE || toks[i].type==TOK_EOF) continue;
        char e2[MAX_TOKEN_LEN * 2];
        jsonEsc(toks[i].lexeme, e2, sizeof(e2));
        if (!first) printf(",");
        printf("{\"type\":\"%s\",\"lexeme\":\"%s\"}",
               TOK_NAMES[toks[i].type], e2);
        first = 0;
    }
    printf("],");

    /* ── IR node array ────────────────────────────────────────────── */
    printf("\"ir\":[");
    for (int i = 0; i < irCount; i++) {
        char d  [MAX_NAME_LEN  * 2];
        char s1 [MAX_NAME_LEN  * 2];
        char op2[MAX_OP_LEN    * 2];
        char s2 [MAX_ARG_LEN   * 2];
        char dt [MAX_TYPE_LEN  * 2];
        jsonEsc(ir[i].dest,  d,   sizeof(d));
        jsonEsc(ir[i].src1,  s1,  sizeof(s1));
        jsonEsc(ir[i].op,    op2, sizeof(op2));
        jsonEsc(ir[i].src2,  s2,  sizeof(s2));
        jsonEsc(ir[i].dtype, dt,  sizeof(dt));
        if (i) printf(",");
        printf("{\"type\":\"%s\","
               "\"dest\":\"%s\",\"src1\":\"%s\","
               "\"op\":\"%s\",\"src2\":\"%s\","
               "\"dtype\":\"%s\"}",
               IR_NAMES[ir[i].type], d, s1, op2, s2, dt);
    }
    printf("],");

    /* ── generated source strings ─────────────────────────────────── */
    jsonEsc(py, esc, sizeof(esc)); printf("\"python\":\"%s\",", esc);
    jsonEsc(cb, esc, sizeof(esc)); printf("\"c\":\"%s\",",      esc);
    jsonEsc(cp, esc, sizeof(esc)); printf("\"cpp\":\"%s\",",    esc);
    jsonEsc(jv, esc, sizeof(esc)); printf("\"java\":\"%s\",",   esc);

    /* ── source language tag ─────────────────────────────────────── */
    printf("\"srclang\":\"%s\",", srcLang);

    /* ── error list ───────────────────────────────────────────────── */
    printf("\"errors\":[");
    for (int i = 0; i < errCount; i++) {
        char e2[MAX_ERROR_LEN * 2];
        jsonEsc(errs[i], e2, sizeof(e2));
        if (i) printf(",");
        printf("\"%s\"", e2);
    }
    printf("]}\n");
}
