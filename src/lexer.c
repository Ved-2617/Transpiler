/*
 * ================================================================
 *  lexer.c  —  UNIVERSAL LEXER IMPLEMENTATION
 *
 *  Single-pass character scanner shared by all four input
 *  languages.  Produces toks[] consumed by parser.c.
 *
 *  Design notes
 *  ─────────────
 *  • Comments (// … \n, /* … *\/, # … \n) are stripped here.
 *  • String literals are tokenised whole (including delimiters).
 *  • Two-character operators are matched before single-character
 *    ones via the TOK2 macro so >> / << / -> / :: etc. are never
 *    split into two single-char tokens.
 *  • Newlines are preserved as TOK_NEWLINE so the Python/custom
 *    parser can use indentation-style block detection.
 * ================================================================
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"
#include "lexer.h"

/* ── identOrKeyword ─────────────────────────────────────────────── */
TokenType identOrKeyword(const char *w) {
    if (!strcmp(w,"if"))        return TOK_IF;
    if (!strcmp(w,"else"))      return TOK_ELSE;
    if (!strcmp(w,"while"))     return TOK_WHILE;
    if (!strcmp(w,"for"))       return TOK_FOR;
    if (!strcmp(w,"return"))    return TOK_RETURN;
    if (!strcmp(w,"print"))     return TOK_PRINT;
    if (!strcmp(w,"true"))      return TOK_TRUE;
    if (!strcmp(w,"false"))     return TOK_FALSE;
    if (!strcmp(w,"int"))       return TOK_INT;
    if (!strcmp(w,"float"))     return TOK_FLOAT_KW;
    if (!strcmp(w,"char"))      return TOK_CHAR_KW;
    if (!strcmp(w,"double"))    return TOK_FLOAT_KW;
    if (!strcmp(w,"void"))      return TOK_VOID;
    if (!strcmp(w,"auto"))      return TOK_AUTO;
    if (!strcmp(w,"bool"))      return TOK_BOOL;
    if (!strcmp(w,"string"))    return TOK_STRING_KW;
    if (!strcmp(w,"String"))    return TOK_STRING_KW;
    if (!strcmp(w,"endl"))      return TOK_ENDL;
    if (!strcmp(w,"cin"))       return TOK_CIN;
    if (!strcmp(w,"cout"))      return TOK_COUT;
    if (!strcmp(w,"using"))     return TOK_USING;
    if (!strcmp(w,"namespace")) return TOK_NAMESPACE;
    if (!strcmp(w,"public"))    return TOK_PUBLIC;
    if (!strcmp(w,"private"))   return TOK_PRIVATE;
    if (!strcmp(w,"protected")) return TOK_PROTECTED;
    if (!strcmp(w,"class"))     return TOK_CLASS;
    if (!strcmp(w,"static"))    return TOK_STATIC;
    if (!strcmp(w,"const"))     return TOK_CONST;
    if (!strcmp(w,"final"))     return TOK_CONST;   /* Java's keyword */
    if (!strcmp(w,"new"))       return TOK_NEW;
    return TOK_IDENT;
}

/* ── lexAll ─────────────────────────────────────────────────────── */
void lexAll(const char *src) {
    int i = 0, n = (int)strlen(src);
    tokCount = 0;

    while (i < n && tokCount < MAX_TOKENS - 1) {
        char c = src[i];

        /* ── comments ───────────────────────────────────────────── */
        if (c=='/' && i+1<n && src[i+1]=='/') {       /* // line   */
            while (i<n && src[i]!='\n') i++;
            continue;
        }
        if (c=='/' && i+1<n && src[i+1]=='*') {       /* block comment */
            i += 2;
            while (i+1<n && !(src[i]=='*' && src[i+1]=='/')) i++;
            i += 2; continue;
        }
        if (c=='#') {                                  /* # Python  */
            while (i<n && src[i]!='\n') i++;
            continue;
        }

        /* ── whitespace ─────────────────────────────────────────── */
        if (c==' ' || c=='\t' || c=='\r') { i++; continue; }

        if (c == '\n') {
            toks[tokCount].type = TOK_NEWLINE;
            strcpy(toks[tokCount].lexeme, "\\n");
            tokCount++; i++; continue;
        }

        /* ── string literals ─────────────────────────────────────── */
        if (c=='"' || c=='\'') {
            char q = c; i++;
            int s = i;
            while (i<n && src[i]!=q) { if (src[i]=='\\') i++; i++; }
            int sl = i - s;
            if (sl >= MAX_TOKEN_LEN - 2) sl = MAX_TOKEN_LEN - 3;
            toks[tokCount].type     = TOK_STRING;
            toks[tokCount].lexeme[0] = q;
            strncpy(toks[tokCount].lexeme + 1, src + s, sl);
            toks[tokCount].lexeme[sl+1] = q;
            toks[tokCount].lexeme[sl+2] = '\0';
            tokCount++; if (i<n) i++; continue;
        }

        /* ── identifiers / keywords ──────────────────────────────── */
        if (isalpha((unsigned char)c) || c=='_') {
            int s = i;
            while (i<n && (isalnum((unsigned char)src[i]) || src[i]=='_')) i++;
            int wl = i - s;
            if (wl >= MAX_TOKEN_LEN) wl = MAX_TOKEN_LEN - 1;
            strncpy(toks[tokCount].lexeme, src+s, wl);
            toks[tokCount].lexeme[wl] = '\0';
            toks[tokCount].type = identOrKeyword(toks[tokCount].lexeme);
            tokCount++; continue;
        }

        /* ── numeric literals ────────────────────────────────────── */
        if (isdigit((unsigned char)c) ||
            (c=='.' && i+1<n && isdigit((unsigned char)src[i+1]))) {
            int s = i;
            while (i<n && (isdigit((unsigned char)src[i]) || src[i]=='.')) i++;
            int nl = i - s;
            if (nl >= MAX_TOKEN_LEN) nl = MAX_TOKEN_LEN - 1;
            strncpy(toks[tokCount].lexeme, src+s, nl);
            toks[tokCount].lexeme[nl] = '\0';
            toks[tokCount].type = TOK_NUMBER;
            tokCount++; continue;
        }

        /* ── two-character operators (must precede single-char) ───── */
#define TOK2(a,b,t,l) \
    if (c==a && i+1<n && src[i+1]==b) { \
        toks[tokCount].type = t; \
        strcpy(toks[tokCount].lexeme, l); \
        tokCount++; i += 2; continue; \
    }
        TOK2('=','=', TOK_EQ,     "==")
        TOK2('!','=', TOK_NEQ,    "!=")
        TOK2('>','=', TOK_GTE,    ">=")
        TOK2('<','=', TOK_LTE,    "<=")
        TOK2('&','&', TOK_AND,    "&&")
        TOK2('|','|', TOK_OR,     "||")
        TOK2('<','<', TOK_LSHIFT, "<<")
        TOK2('>','>', TOK_RSHIFT, ">>")
        TOK2('-','>', TOK_ARROW,  "->")
        TOK2(':',':', TOK_DCOLON, "::")
#undef TOK2

        /* ── single-character tokens ─────────────────────────────── */
        {
            TokenType st = TOK_UNKNOWN;
            char sl[3] = {c, '\0', '\0'};
            switch (c) {
                case '=': st = TOK_ASSIGN;    break;
                case '+': st = TOK_PLUS;      break;
                case '-': st = TOK_MINUS;     break;
                case '*': st = TOK_STAR;      break;
                case '/': st = TOK_SLASH;     break;
                case '%': st = TOK_PERCENT;   break;
                case '>': st = TOK_GT;        break;
                case '<': st = TOK_LT;        break;
                case '(': st = TOK_LPAREN;    break;
                case ')': st = TOK_RPAREN;    break;
                case '{': st = TOK_LBRACE;    break;
                case '}': st = TOK_RBRACE;    break;
                case '[': st = TOK_LBRACKET;  break;
                case ']': st = TOK_RBRACKET;  break;
                case ';': st = TOK_SEMICOLON; break;
                case ':': st = TOK_COLON;     break;
                case ',': st = TOK_COMMA;     break;
                case '.': st = TOK_DOT;       break;
                case '&': st = TOK_AMP;       break;
                case '|': st = TOK_PIPE;      break;
                case '!': st = TOK_NOT;       break;
                case '#': st = TOK_HASH;      break;
                default: {
                    char msg[64];
                    snprintf(msg, 64, "[LEXER] Unknown char '%c'", c);
                    addErr(msg);
                    i++; continue;
                }
            }
            toks[tokCount].type = st;
            strcpy(toks[tokCount].lexeme, sl);
            tokCount++; i++;
        }
    }

    toks[tokCount].type = TOK_EOF;
    strcpy(toks[tokCount].lexeme, "EOF");
    tokCount++;
}
