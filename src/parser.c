/*
 * ================================================================
 *  parser.c  —  LANGUAGE PARSER IMPLEMENTATIONS
 *
 *  Internal structure
 *  ──────────────────
 *  Cursor helpers (cur / adv / peekAt / skipNL / skipSC)
 *    Thin wrappers around the global `pos` index into toks[].
 *
 *  Low-level readers (readOperand / collectArgs / collectBraceList)
 *    Extract a single value or a balanced bracketed argument list
 *    as a raw string without building sub-trees.
 *
 *  pushIR()
 *    The only function that writes to ir[].  All parse paths go
 *    through here so the IR stays consistent.
 *
 *  tryMemberStatement()
 *    Lookahead helper: peeks for `ident . member` or `ident->member`
 *    and, if found, emits IR_CALL / IR_FIELD_ACCESS / IR_FIELD_SET.
 *    Returns 1 on success so callers can skip their fallback.
 *
 *  sharedStmt()
 *    Core statement parser used by all four language front-ends and
 *    by parseClassBody() for method / constructor bodies.  Handles:
 *    braces, return, if/else, while, for, printf, cout, System.out,
 *    member access, template containers, array declarations,
 *    assignments, compound-assignments, and binary expressions.
 *
 *  parseClassBody() / parseClassMember()
 *    Walk the interior of a class {…} block, emitting IR_FIELD_DECL,
 *    IR_CTOR_START/END, IR_METHOD_START/END, IR_MAIN_START/END.
 *
 *  parsePython / parseC / parseCpp / parseJava
 *    Language-specific outer loops that handle top-level structure
 *    (preprocessor lines, `int main()` wrappers, class declarations,
 *    `using namespace`, etc.) before delegating to sharedStmt().
 * ================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"
#include "containers.h"
#include "parser.h"

/* ═══════════════════════════════════════════════════════════════════
   CURSOR HELPERS
   Thin wrappers around the global `pos` index into toks[].
═══════════════════════════════════════════════════════════════════ */

static inline Token cur(void) {
    return (pos < tokCount) ? toks[pos] : toks[tokCount-1];
}
static inline Token adv(void) {
    Token t = cur();
    if (pos < tokCount - 1) pos++;
    return t;
}
static inline Token peekAt(int off) {
    int p = pos + off;
    if (p >= tokCount) p = tokCount - 1;
    if (p < 0) p = 0;
    return toks[p];
}
static inline void skipNL(void) {
    while (cur().type == TOK_NEWLINE) adv();
}
static inline void skipSC(void) {
    while (cur().type == TOK_SEMICOLON || cur().type == TOK_NEWLINE) adv();
}

/* ─── classification helpers ─────────────────────────────────────── */
static inline int isTypeKw(TokenType t) {
    return t==TOK_INT || t==TOK_FLOAT_KW || t==TOK_CHAR_KW ||
           t==TOK_VOID || t==TOK_AUTO || t==TOK_BOOL || t==TOK_STRING_KW;
}
static inline int isArith(TokenType t) {
    return t==TOK_PLUS || t==TOK_MINUS || t==TOK_STAR ||
           t==TOK_SLASH || t==TOK_PERCENT;
}
static inline int isModifierKw(TokenType t) {
    return t==TOK_PUBLIC || t==TOK_PRIVATE || t==TOK_PROTECTED ||
           t==TOK_STATIC || t==TOK_CONST;
}

/* ═══════════════════════════════════════════════════════════════════
   pushIR
   The single write-path into ir[].
═══════════════════════════════════════════════════════════════════ */
static void pushIR(IRType tp,
                   const char *dest, const char *s1,
                   const char *op,   const char *s2,
                   const char *dt) {
    if (irCount >= MAX_IR_NODES) return;
    IRNode nd; memset(&nd, 0, sizeof(nd));
    nd.type = tp;
    if (dest) strncpy(nd.dest,  dest, MAX_NAME_LEN-1);
    if (s1)   strncpy(nd.src1,  s1,   MAX_NAME_LEN-1);
    if (op)   strncpy(nd.op,    op,   MAX_OP_LEN-1);
    if (s2)   strncpy(nd.src2,  s2,   MAX_ARG_LEN-1);
    if (dt)   strncpy(nd.dtype, dt,   MAX_TYPE_LEN-1);
    ir[irCount++] = nd;
}

/* ═══════════════════════════════════════════════════════════════════
   LOW-LEVEL READERS
═══════════════════════════════════════════════════════════════════ */

/*
 * Read one operand: identifier, number, bool literal, or
 * array-subscript expression like arr[i] (returned as "arr[i]").
 * Handles unary minus.  Returns a synthetic token on failure.
 */
static Token readOperand(void) {
    skipNL();
    if (cur().type==TOK_IDENT   || cur().type==TOK_NUMBER ||
        cur().type==TOK_TRUE    || cur().type==TOK_FALSE) {
        Token t = adv();
        if (cur().type == TOK_LBRACKET) {
            adv();
            char idx[MAX_TOKEN_LEN] = "0";
            if (cur().type==TOK_NUMBER || cur().type==TOK_IDENT) {
                strncpy(idx, cur().lexeme, MAX_TOKEN_LEN-1); adv();
            }
            if (cur().type == TOK_RBRACKET) adv();
            char combined[MAX_TOKEN_LEN];
            snprintf(combined, MAX_TOKEN_LEN, "%s[%s]", t.lexeme, idx);
            strncpy(t.lexeme, combined, MAX_TOKEN_LEN-1);
        }
        return t;
    }
    if (cur().type == TOK_MINUS) {
        adv();
        Token n = adv();
        Token r;
        snprintf(r.lexeme, MAX_TOKEN_LEN, "-%s", n.lexeme);
        r.type = TOK_NUMBER;
        return r;
    }
    Token bad; strcpy(bad.lexeme, "0"); bad.type = TOK_NUMBER; return bad;
}

/* Skip to the end of the current statement without consuming it. */
static void skipLine(void) {
    while (cur().type!=TOK_NEWLINE && cur().type!=TOK_SEMICOLON &&
           cur().type!=TOK_RBRACE  && cur().type!=TOK_EOF) adv();
}

/*
 * Consume a balanced parenthesised argument list, starting at '('.
 * Returns the raw comma-separated interior text in `out`.
 *   (i+1, 0)  →  "i+1,0"
 */
static void collectArgs(char *out, int sz) {
    out[0] = '\0';
    if (cur().type == TOK_LPAREN) adv();
    int depth = 1;
    while (cur().type != TOK_EOF && depth > 0) {
        if (cur().type==TOK_LPAREN) { depth++; app(out,"(",sz); adv(); continue; }
        if (cur().type==TOK_RPAREN) { depth--; if(!depth){adv();break;} app(out,")",sz); adv(); continue; }
        if (cur().type==TOK_COMMA)  { app(out,",",sz); adv(); continue; }
        app(out, cur().lexeme, sz);
        adv();
    }
}

/*
 * Consume a brace-delimited initialiser list starting at '{'.
 * Returns the whole thing including braces in `out`.
 */
static void collectBraceList(char *out, int sz) {
    out[0] = '\0'; app(out,"{",sz);
    if (cur().type == TOK_LBRACE) adv();
    int depth = 1;
    while (cur().type != TOK_EOF && depth > 0) {
        if (cur().type==TOK_LBRACE) { depth++; app(out,"{",sz); adv(); continue; }
        if (cur().type==TOK_RBRACE) { depth--; if(!depth){adv();break;} app(out,"}",sz); adv(); continue; }
        if (cur().type==TOK_COMMA)  { app(out,",",sz); adv(); continue; }
        app(out, cur().lexeme, sz);
        adv();
    }
    app(out,"}",sz);
}

/*
 * Consume a parenthesised formal-parameter list starting at '('.
 * Returns a space-separated flat representation in `out`.
 *   (int a, int b)  →  "int a int b"
 */
static void collectParams(char *out, int sz) {
    out[0] = '\0';
    if (cur().type == TOK_LPAREN) adv();
    int first = 1;
    while (cur().type != TOK_RPAREN && cur().type != TOK_EOF) {
        if (cur().type == TOK_COMMA) { adv(); continue; }
        if (!first) app(out," ",sz);
        app(out, cur().lexeme, sz);
        first = 0; adv();
    }
    if (cur().type == TOK_RPAREN) adv();
}

/* ═══════════════════════════════════════════════════════════════════
   MEMBER-STATEMENT LOOKAHEAD
   Peeks for  ident . member  or  ident -> member  and emits the
   appropriate IR node.  Returns 1 if a statement was consumed.
═══════════════════════════════════════════════════════════════════ */
static int tryMemberStatement(void) {
    if (cur().type != TOK_IDENT) return 0;
    TokenType nxt = peekAt(1).type;
    if (nxt != TOK_DOT && nxt != TOK_ARROW) return 0;

    Token obj = adv();
    char accessOp[4];
    strcpy(accessOp, cur().type==TOK_ARROW ? "->" : ".");
    adv(); /* eat . or -> */

    if (cur().type != TOK_IDENT) {
        addErr("[PARSER] Expected member name after '.'/'->'");
        skipLine(); return 1;
    }
    Token member = adv();

    if (cur().type == TOK_LPAREN) {
        /* obj.method(args)  →  IR_CALL (no result variable) */
        char args[MAX_ARG_LEN]; collectArgs(args, sizeof(args));
        skipSC();
        pushIR(IR_CALL, obj.lexeme, member.lexeme, accessOp, args, "");
        return 1;
    }
    if (cur().type == TOK_ASSIGN) {
        /* obj.field = val  →  IR_FIELD_SET */
        adv();
        Token val = readOperand();
        skipSC();
        pushIR(IR_FIELD_SET, obj.lexeme, member.lexeme, accessOp, val.lexeme, "");
        return 1;
    }
    /* bare read: obj.field;  (side-effect-free but preserve for round-trip) */
    pushIR(IR_FIELD_ACCESS, "", obj.lexeme, accessOp, member.lexeme, "");
    skipSC();
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
   SHARED STATEMENT PARSER
   Called by all four language parsers and by class-body parsing.
   Handles every statement form that is common across languages.
═══════════════════════════════════════════════════════════════════ */

/* Forward declarations for mutual recursion */
static void parseClassBody(const char *className);
void parsePyStmt(void); /* used internally and by parsePython */

void sharedStmt(void) {
    skipNL();
    if (cur().type == TOK_EOF) return;

    /* ── { ── */
    if (cur().type == TOK_LBRACE) {
        adv(); pushIR(IR_BLOCK_START,"","","","",""); return;
    }
    /* ── } ── */
    if (cur().type == TOK_RBRACE) {
        adv(); pushIR(IR_BLOCK_END,  "","","","",""); return;
    }

    /* ── return [expr] ; ── */
    if (cur().type == TOK_RETURN) {
        adv();
        if (cur().type==TOK_SEMICOLON || cur().type==TOK_NEWLINE ||
            cur().type==TOK_RBRACE) {
            pushIR(IR_RETURN,"","","","",""); skipSC(); return;
        }
        Token v = readOperand(); skipSC();
        pushIR(IR_RETURN,"",v.lexeme,"","",""); return;
    }

    /* ── if ( cond ) ── */
    if (cur().type == TOK_IF) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if (cur().type==TOK_RPAREN) adv();
        pushIR(IR_IF,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    /* ── else ── */
    if (cur().type == TOK_ELSE) {
        adv(); pushIR(IR_ELSE,"","","","","");
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    /* ── while ( cond ) ── */
    if (cur().type == TOK_WHILE) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if (cur().type==TOK_RPAREN) adv();
        pushIR(IR_WHILE,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    /* ── for ( [type] i=start; i op limit; i++ ) ── */
    if (cur().type == TOK_FOR) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        char dtype[16]="int";
        if (isTypeKw(cur().type)) strncpy(dtype, adv().lexeme, 15);
        Token ivar   = adv();
        if (cur().type==TOK_ASSIGN) adv();
        Token istart = readOperand(); skipSC();
        Token cl     = readOperand(); (void)cl;
        Token cop    = adv();
        Token cr     = readOperand(); skipSC();
        while (cur().type!=TOK_RPAREN && cur().type!=TOK_EOF) adv();
        if (cur().type==TOK_RPAREN) adv();
        pushIR(IR_FOR, ivar.lexeme, istart.lexeme, cop.lexeme, cr.lexeme, dtype);
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    /* ── printf("fmt", x) ── */
    if (cur().type==TOK_IDENT && !strcmp(cur().lexeme,"printf")) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        if (cur().type==TOK_STRING) adv();   /* skip format string */
        if (cur().type==TOK_COMMA)  adv();
        Token v = readOperand();
        while (cur().type!=TOK_RPAREN && cur().type!=TOK_SEMICOLON &&
               cur().type!=TOK_EOF) adv();
        if (cur().type==TOK_RPAREN) adv();
        if (strcmp(v.lexeme,"0")) pushIR(IR_PRINT, v.lexeme,"","","","");
        return;
    }

    /* ── cout << x [<< endl] ── */
    if (cur().type == TOK_COUT) {
        adv();
        while (cur().type == TOK_LSHIFT) {
            adv();
            if (cur().type==TOK_ENDL || cur().type==TOK_NEWLINE ||
                cur().type==TOK_SEMICOLON) {
                if (cur().type==TOK_ENDL) adv();
                break;
            }
            Token v = readOperand();
            if (strcmp(v.lexeme,"0")) pushIR(IR_PRINT, v.lexeme,"","","","");
        }
        return;
    }

    /* ── System.out.println(x) ── */
    if (cur().type==TOK_IDENT && !strcmp(cur().lexeme,"System")) {
        adv();
        while (cur().type==TOK_DOT) { adv(); adv(); }  /* .out.println */
        if (cur().type==TOK_LPAREN) adv();
        Token v = readOperand();
        if (cur().type==TOK_RPAREN) adv();
        if (strcmp(v.lexeme,"0")) pushIR(IR_PRINT, v.lexeme,"","","","");
        return;
    }

    /* ── member access / method call as a statement ── */
    if (tryMemberStatement()) return;

    /* ── variable / array / container declaration or assignment ──
       Pattern: [type]? ident < template > varName (…) ;    container
                [type]? ident [ size ] ;                     array
                [type]? ident = RHS ;                        assign
                [type]? ident op= RHS ;                      compound assign  */
    char dtype[16] = "";
    if (isTypeKw(cur().type)) strncpy(dtype, adv().lexeme, 15);

    if (cur().type == TOK_IDENT) {
        Token dest = adv();

        /* ── template container: vector<int> name(…) ── */
        if (cur().type == TOK_LT) {
            char tplName[32]; strncpy(tplName, dest.lexeme, 31); tplName[31]='\0';
            char kind[16] = "vector";
            if (!strcmp(tplName,"map") || !strcmp(tplName,"unordered_map"))
                strcpy(kind,"map");
            else if (!strcmp(tplName,"list") || !strcmp(tplName,"LinkedList"))
                strcpy(kind,"list");
            else if (!strcmp(tplName,"set") || !strcmp(tplName,"unordered_set"))
                strcpy(kind,"set");

            adv(); /* eat '<' */
            char typeParts[2][20] = {"int","int"};
            int typeIdx=0, capturedThis=0, depth=1;
            while (cur().type!=TOK_EOF && depth>0) {
                if (cur().type==TOK_LT)   { depth++; adv(); continue; }
                if (cur().type==TOK_GT)   { depth--; adv(); continue; }
                if (cur().type==TOK_RSHIFT){ depth-=2; adv(); continue; }
                if (cur().type==TOK_COMMA && depth==1){
                    typeIdx=1; capturedThis=0; adv(); continue;
                }
                if (depth==1 && !capturedThis &&
                    (isTypeKw(cur().type)||cur().type==TOK_IDENT)) {
                    strncpy(typeParts[typeIdx], cur().lexeme, 19);
                    capturedThis=1;
                }
                adv();
            }
            skipNL();
            if (cur().type != TOK_IDENT) { skipLine(); return; }
            Token varName = adv();

            char elemCombined[42];
            if (!strcmp(kind,"map"))
                snprintf(elemCombined, 42, "%s:%s", typeParts[0], typeParts[1]);
            else
                strncpy(elemCombined, typeParts[0], 41);

            char ctorArgs[40] = "";
            if (cur().type==TOK_LPAREN) {
                collectArgs(ctorArgs, sizeof(ctorArgs));
            } else if (cur().type==TOK_ASSIGN) {
                adv();
                if (cur().type==TOK_LBRACE) collectBraceList(ctorArgs, sizeof(ctorArgs));
                else { Token v=readOperand(); strncpy(ctorArgs,v.lexeme,39); }
            }
            pushIR(IR_CONTAINER_DECL, varName.lexeme, elemCombined, kind, ctorArgs,"");
            addContainer(varName.lexeme, kind, elemCombined, ctorArgs);
            skipSC(); return;
        }

        /* ── fixed-size array: type name[size] ── */
        if (cur().type == TOK_LBRACKET) {
            adv();
            char sizeStr[24] = "";
            if (cur().type==TOK_NUMBER || cur().type==TOK_IDENT) {
                strncpy(sizeStr, cur().lexeme, 23); adv();
            }
            if (cur().type==TOK_RBRACKET) adv();
            if (cur().type==TOK_ASSIGN) {
                adv();
                if (cur().type==TOK_LBRACE) { char tmp[64]; collectBraceList(tmp,sizeof(tmp)); }
                else readOperand();
            }
            pushIR(IR_CONTAINER_DECL, dest.lexeme, dtype[0]?dtype:"int", "array", sizeStr,"");
            addContainer(dest.lexeme, "array", dtype[0]?dtype:"int", sizeStr);
            skipSC(); return;
        }

        /* ── assignment / new / member-call on RHS ── */
        if (cur().type == TOK_ASSIGN) {
            adv();

            /* new ClassName(args) */
            if (cur().type == TOK_NEW) {
                adv();
                Token cls = (cur().type==TOK_IDENT) ? adv() : dest;
                char args[MAX_ARG_LEN]="";
                if (cur().type==TOK_LPAREN) collectArgs(args,sizeof(args));
                pushIR(IR_NEW, dest.lexeme, cls.lexeme,"",args,"");
                skipSC(); return;
            }

            /* obj.method(args) or obj.field on RHS */
            if (cur().type==TOK_IDENT &&
                (peekAt(1).type==TOK_DOT || peekAt(1).type==TOK_ARROW)) {
                Token obj = adv();
                char accessOp[4];
                strcpy(accessOp, cur().type==TOK_ARROW ? "->" : ".");
                adv();
                if (cur().type==TOK_IDENT) {
                    Token member = adv();
                    if (cur().type==TOK_LPAREN) {
                        char args[MAX_ARG_LEN]; collectArgs(args,sizeof(args));
                        /* result stored in dest */
                        pushIR(IR_CALL, obj.lexeme, member.lexeme, accessOp,
                               args, dest.lexeme);
                    } else {
                        pushIR(IR_FIELD_ACCESS, dest.lexeme, obj.lexeme,
                               accessOp, member.lexeme,"");
                    }
                }
                skipSC(); return;
            }

            /* plain RHS: expr or binary expr */
            Token s1 = readOperand();
            if (isArith(cur().type)) {
                Token op=adv(), s2=readOperand();
                pushIR(IR_BINOP, dest.lexeme, s1.lexeme, op.lexeme, s2.lexeme, dtype);
            } else {
                pushIR(IR_ASSIGN, dest.lexeme, s1.lexeme,"","", dtype);
            }
            skipSC(); return;
        }

        /* ── compound assignment: x += val  etc. ── */
        if (cur().type==TOK_PLUS || cur().type==TOK_MINUS ||
            cur().type==TOK_STAR || cur().type==TOK_SLASH) {
            char op2[4]; strncpy(op2, cur().lexeme, 3); adv();
            if (cur().type==TOK_ASSIGN) {
                adv();
                Token s2 = readOperand();
                pushIR(IR_BINOP, dest.lexeme, dest.lexeme, op2, s2.lexeme, dtype);
                skipSC(); return;
            }
        }

        /* unrecognised statement after an identifier */
        char msg[256];
        snprintf(msg, 256,
            "[PARSER] Unexpected token '%s' after identifier '%s': "
            "statement not recognised",
            cur().lexeme, dest.lexeme);
        addErr(msg);
    }

    skipLine();
}

/* ═══════════════════════════════════════════════════════════════════
   CLASS BODY PARSING
   Shared by parseCpp and parseJava.
═══════════════════════════════════════════════════════════════════ */

/*
 * Parse one class member (field, constructor, method, or main()).
 * `sectionMod` carries the last "public:"/"private:" label seen
 * (C++ style).  Returns 1 if a main() was parsed.
 */
static int parseClassMember(const char *className, const char *sectionMod) {
    skipNL();

    /* Collect inline access/storage modifiers (Java style) */
    char mods[40]=""; int modCount=0;
    while (isModifierKw(cur().type)) {
        if (modCount++) app(mods,",",sizeof(mods));
        app(mods, adv().lexeme, sizeof(mods));
    }
    if (!mods[0] && sectionMod && sectionMod[0])
        strncpy(mods, sectionMod, sizeof(mods)-1);

    int isStaticMain = strstr(mods,"static") != NULL;

    /* Optional return-type keyword */
    char typeStr[24] = "";
    if (isTypeKw(cur().type)) strncpy(typeStr, adv().lexeme, 23);

    if (cur().type != TOK_IDENT) { skipLine(); skipSC(); return 0; }
    Token nameTok = adv();

    /* ── Java public static void main ── */
    if (isStaticMain && !strcmp(nameTok.lexeme,"main")) {
        char params[80]; collectParams(params, sizeof(params));
        skipNL();
        if (cur().type==TOK_LBRACE) adv();
        pushIR(IR_MAIN_START,"main","","","","");
        int depth=1;
        while (cur().type!=TOK_EOF && depth>0) {
            skipNL();
            if (cur().type==TOK_RBRACE) { depth--; if(!depth){adv();break;} }
            if (cur().type==TOK_LBRACE)   depth++;
            sharedStmt(); skipSC();
        }
        pushIR(IR_MAIN_END,"main","","","","");
        return 1;
    }

    /* ── constructor: name matches className, no return type, followed by ( ── */
    if (!typeStr[0] && !strcmp(nameTok.lexeme,className) &&
        cur().type==TOK_LPAREN) {
        char params[80]; collectParams(params, sizeof(params));
        /* optional C++ member-initialiser list:  : a(a), b(b) */
        if (cur().type==TOK_COLON)
            while (cur().type!=TOK_LBRACE && cur().type!=TOK_EOF) adv();
        skipNL();
        if (cur().type==TOK_LBRACE) adv();
        pushIR(IR_CTOR_START, className,"", mods, params,"");
        int depth=1;
        while (cur().type!=TOK_EOF && depth>0) {
            skipNL();
            if (cur().type==TOK_RBRACE) { depth--; if(!depth){adv();break;} }
            if (cur().type==TOK_LBRACE)   depth++;
            sharedStmt(); skipSC();
        }
        pushIR(IR_CTOR_END, className,"","","","");
        return 0;
    }

    /* ── method: name ( params ) { body } ── */
    if (cur().type == TOK_LPAREN) {
        char params[80]; collectParams(params, sizeof(params));
        skipNL();
        if (cur().type==TOK_LBRACE) adv();
        pushIR(IR_METHOD_START, nameTok.lexeme, typeStr[0]?typeStr:"void",
               mods, params,"");
        int depth=1;
        while (cur().type!=TOK_EOF && depth>0) {
            skipNL();
            if (cur().type==TOK_RBRACE) { depth--; if(!depth){adv();break;} }
            if (cur().type==TOK_LBRACE)   depth++;
            sharedStmt(); skipSC();
        }
        pushIR(IR_METHOD_END, nameTok.lexeme,"","","","");
        return 0;
    }

    /* ── field declaration: name [= initVal] ; ── */
    char initVal[MAX_NAME_LEN]="";
    if (cur().type==TOK_ASSIGN) {
        adv();
        Token v = readOperand();
        strncpy(initVal, v.lexeme, MAX_NAME_LEN-1);
    }
    pushIR(IR_FIELD_DECL, nameTok.lexeme, initVal, mods,"",
           typeStr[0]?typeStr:"int");
    skipSC();
    return 0;
}

/*
 * Parse the { … } body of a class, handling C++ section labels
 * (public: / private: / protected:) and Java inline modifiers.
 * Consumes the closing '}'.
 */
static void parseClassBody(const char *className) {
    char sectionMod[16] = "";
    while (cur().type != TOK_EOF) {
        skipNL();
        if (cur().type==TOK_RBRACE) { adv(); break; }

        /* C++ section label: public: / private: / protected: */
        if ((cur().type==TOK_PUBLIC || cur().type==TOK_PRIVATE ||
             cur().type==TOK_PROTECTED) && peekAt(1).type==TOK_COLON) {
            strncpy(sectionMod, adv().lexeme, 15);
            adv(); /* eat ':' */
            continue;
        }
        parseClassMember(className, sectionMod);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   PYTHON / CUSTOM PARSER
═══════════════════════════════════════════════════════════════════ */

void parsePyStmt(void) {
    skipNL();
    if (cur().type == TOK_EOF) return;

    /* print x  or  print(x) */
    if (cur().type == TOK_PRINT) {
        adv();
        int paren = (cur().type==TOK_LPAREN); if (paren) adv();
        Token v = readOperand();
        if (paren && cur().type==TOK_RPAREN) adv();
        pushIR(IR_PRINT, v.lexeme,"","","",""); return;
    }

    /* if cond :  */
    if (cur().type == TOK_IF) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if (cur().type==TOK_RPAREN) adv();
        if (cur().type==TOK_COLON)  adv();
        pushIR(IR_IF,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    /* else : */
    if (cur().type == TOK_ELSE) {
        adv(); if (cur().type==TOK_COLON) adv();
        pushIR(IR_ELSE,"","","","",""); return;
    }

    /* while cond : */
    if (cur().type == TOK_WHILE) {
        adv();
        if (cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if (cur().type==TOK_RPAREN) adv();
        if (cur().type==TOK_COLON)  adv();
        pushIR(IR_WHILE,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if (cur().type==TOK_LBRACE) { adv(); pushIR(IR_BLOCK_START,"","","","",""); }
        return;
    }

    if (cur().type==TOK_RBRACE) { adv(); pushIR(IR_BLOCK_END,"","","","",""); return; }

    if (cur().type==TOK_RETURN) {
        adv(); Token v=readOperand();
        pushIR(IR_RETURN,"",v.lexeme,"","",""); return;
    }

    if (tryMemberStatement()) return;

    /* optional type hint then  name = expr */
    char dtype[16]="";
    if (isTypeKw(cur().type)) strncpy(dtype, adv().lexeme, 15);

    if (cur().type == TOK_IDENT) {
        Token dest = adv();
        if (cur().type == TOK_ASSIGN) {
            adv();
            /* RHS: obj.method() or obj.field */
            if (cur().type==TOK_IDENT &&
                (peekAt(1).type==TOK_DOT || peekAt(1).type==TOK_ARROW)) {
                Token obj = adv();
                char accessOp[4];
                strcpy(accessOp, cur().type==TOK_ARROW ? "->" : "."); adv();
                if (cur().type==TOK_IDENT) {
                    Token member = adv();
                    if (cur().type==TOK_LPAREN) {
                        char args[MAX_ARG_LEN]; collectArgs(args,sizeof(args));
                        pushIR(IR_CALL, obj.lexeme, member.lexeme, accessOp,
                               args, dest.lexeme);
                    } else {
                        pushIR(IR_FIELD_ACCESS, dest.lexeme, obj.lexeme,
                               accessOp, member.lexeme,"");
                    }
                }
                return;
            }
            Token s1 = readOperand();
            if (isArith(cur().type)) {
                Token op=adv(), s2=readOperand();
                pushIR(IR_BINOP, dest.lexeme, s1.lexeme, op.lexeme, s2.lexeme, dtype);
            } else {
                pushIR(IR_ASSIGN, dest.lexeme, s1.lexeme,"","", dtype);
            }
            return;
        }
    }
    skipLine();
}

void parsePython(void) {
    pos=0; irCount=0; containerCount=0;
    while (cur().type != TOK_EOF) {
        skipNL(); if (cur().type==TOK_EOF) break;
        parsePyStmt(); skipSC();
    }
}

/* ═══════════════════════════════════════════════════════════════════
   HELPER: outer-level typed declaration with member-call RHS support
   Used by both parseC and parseCpp when they see a type keyword
   outside of main().
═══════════════════════════════════════════════════════════════════ */
static void handleOuterTypedDecl(const char *dtype) {
    if (cur().type != TOK_IDENT) { skipSC(); return; }
    Token dest = adv();

    /* array: type name[size] */
    if (cur().type == TOK_LBRACKET) {
        adv();
        char sizeStr[24]="";
        if (cur().type==TOK_NUMBER||cur().type==TOK_IDENT){
            strncpy(sizeStr,cur().lexeme,23); adv();
        }
        if (cur().type==TOK_RBRACKET) adv();
        if (cur().type==TOK_ASSIGN) {
            adv();
            if (cur().type==TOK_LBRACE) { char tmp[64]; collectBraceList(tmp,sizeof(tmp)); }
            else readOperand();
        }
        pushIR(IR_CONTAINER_DECL, dest.lexeme, dtype, "array", sizeStr,"");
        addContainer(dest.lexeme, "array", dtype, sizeStr);
        skipSC(); return;
    }

    /* assignment */
    if (cur().type == TOK_ASSIGN) {
        adv();
        /* obj.method() on RHS */
        if (cur().type==TOK_IDENT &&
            (peekAt(1).type==TOK_DOT || peekAt(1).type==TOK_ARROW)) {
            Token obj=adv();
            char accessOp[4]; strcpy(accessOp,cur().type==TOK_ARROW?"->":"."); adv();
            if (cur().type==TOK_IDENT) {
                Token member=adv();
                if (cur().type==TOK_LPAREN) {
                    char args[MAX_ARG_LEN]; collectArgs(args,sizeof(args));
                    pushIR(IR_CALL, obj.lexeme, member.lexeme, accessOp,
                           args, dest.lexeme);
                } else {
                    pushIR(IR_FIELD_ACCESS, dest.lexeme, obj.lexeme,
                           accessOp, member.lexeme,"");
                }
            }
            skipSC(); return;
        }
        Token s1 = readOperand();
        if (isArith(cur().type)) {
            Token op=adv(), s2=readOperand();
            pushIR(IR_BINOP, dest.lexeme, s1.lexeme, op.lexeme, s2.lexeme, dtype);
        } else {
            pushIR(IR_ASSIGN, dest.lexeme, s1.lexeme,"","", dtype);
        }
        skipSC(); return;
    }
    skipSC();
}

/* ═══════════════════════════════════════════════════════════════════
   C PARSER
═══════════════════════════════════════════════════════════════════ */

void parseC(void) {
    pos=0; irCount=0; containerCount=0;
    int mainDepth = -1;   /* -1 = not yet inside main() body */

    while (cur().type != TOK_EOF) {
        skipNL(); if (cur().type==TOK_EOF) break;

        /* strip preprocessor lines */
        if (cur().type==TOK_HASH || cur().type==TOK_INCLUDE) {
            while (cur().type!=TOK_NEWLINE && cur().type!=TOK_EOF) adv();
            continue;
        }

        /* outside main(): look for int/void main() { or a typed decl */
        if (mainDepth<0 && (cur().type==TOK_INT || cur().type==TOK_VOID)) {
            Token saved = adv();
            if (cur().type==TOK_IDENT && !strcmp(cur().lexeme,"main")) {
                while (cur().type!=TOK_LBRACE && cur().type!=TOK_EOF) adv();
                if (cur().type==TOK_LBRACE) { adv(); mainDepth=1; }
                continue;
            }
            char dtype[16]; strncpy(dtype, saved.lexeme, 15);
            handleOuterTypedDecl(dtype);
            continue;
        }

        /* inside main(): track brace depth so we consume its closing } */
        if (mainDepth>=1 && cur().type==TOK_RBRACE) {
            mainDepth--;
            if (mainDepth==0) { adv(); break; }
        } else if (mainDepth>=1 && cur().type==TOK_LBRACE) {
            mainDepth++;
        }

        sharedStmt(); skipSC();
    }
}

/* ═══════════════════════════════════════════════════════════════════
   C++ PARSER
═══════════════════════════════════════════════════════════════════ */

void parseCpp(void) {
    pos=0; irCount=0; containerCount=0;
    int mainDepth = -1;

    while (cur().type != TOK_EOF) {
        skipNL(); if (cur().type==TOK_EOF) break;

        /* strip preprocessor and using-namespace */
        if (cur().type==TOK_HASH || cur().type==TOK_INCLUDE) {
            while (cur().type!=TOK_NEWLINE && cur().type!=TOK_EOF) adv();
            continue;
        }
        if (cur().type==TOK_USING) {
            while (cur().type!=TOK_SEMICOLON && cur().type!=TOK_NEWLINE &&
                   cur().type!=TOK_EOF) adv();
            skipSC(); continue;
        }

        /* class declaration at top level */
        if (mainDepth<0 && cur().type==TOK_CLASS) {
            adv();
            char className[MAX_NAME_LEN]="";
            if (cur().type==TOK_IDENT) strncpy(className, adv().lexeme, MAX_NAME_LEN-1);
            while (cur().type!=TOK_LBRACE && cur().type!=TOK_EOF) adv();
            if (cur().type==TOK_LBRACE) adv();
            pushIR(IR_CLASS_START, className,"","","","");
            parseClassBody(className);
            pushIR(IR_CLASS_END,   className,"","","","");
            skipSC(); continue;
        }

        /* outside main(): int/void main() { or typed decl */
        if (mainDepth<0 && (cur().type==TOK_INT || cur().type==TOK_VOID)) {
            Token saved = adv();
            if (cur().type==TOK_IDENT && !strcmp(cur().lexeme,"main")) {
                while (cur().type!=TOK_LBRACE && cur().type!=TOK_EOF) adv();
                if (cur().type==TOK_LBRACE) { adv(); mainDepth=1; }
                continue;
            }
            char dtype[16]; strncpy(dtype, saved.lexeme, 15);
            handleOuterTypedDecl(dtype);
            continue;
        }

        /* inside main(): track brace depth */
        if (mainDepth>=1 && cur().type==TOK_RBRACE) {
            mainDepth--;
            if (mainDepth==0) { adv(); break; }
        } else if (mainDepth>=1 && cur().type==TOK_LBRACE) {
            mainDepth++;
        }

        sharedStmt(); skipSC();
    }
}

/* ═══════════════════════════════════════════════════════════════════
   JAVA PARSER
═══════════════════════════════════════════════════════════════════ */

void parseJava(void) {
    pos=0; irCount=0; containerCount=0;

    /* Decide whether the source has a class wrapper */
    int hasClass = 0;
    for (int i=0; i<tokCount; i++)
        if (toks[i].type==TOK_CLASS) { hasClass=1; break; }

    if (hasClass) {
        while (cur().type!=TOK_CLASS && cur().type!=TOK_EOF) adv();
        if (cur().type==TOK_CLASS) {
            adv();
            char className[MAX_NAME_LEN]="";
            if (cur().type==TOK_IDENT) strncpy(className, adv().lexeme, MAX_NAME_LEN-1);
            while (cur().type!=TOK_LBRACE && cur().type!=TOK_EOF) adv();
            if (cur().type==TOK_LBRACE) adv();
            pushIR(IR_CLASS_START, className,"","","","");
            parseClassBody(className);
            pushIR(IR_CLASS_END,   className,"","","","");
        }
        return;
    }

    /* Fallback: bare statements with no class/main wrapper */
    pos=0;
    while (cur().type != TOK_EOF) {
        skipNL(); if (cur().type==TOK_EOF) break;
        sharedStmt(); skipSC();
    }
}
