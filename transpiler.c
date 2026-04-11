/*
 * ================================================================
 *  MULTI-LANGUAGE TRANSPILER  v2.1  (bug-fixed)
 *  Input languages : python / c / cpp / java / custom
 *  Output languages: Python, C, C++, Java
 *
 *  stdin protocol:
 *    Line 1: language tag  (python|c|cpp|java|custom)
 *    Rest  : actual source code
 *
 *  stdout: one JSON object
 *    { tokens, ir, python, c, cpp, java, srclang, errors }
 *
 *  Compiler phases:
 *    1. Universal LEXER   → shared Token[]
 *    2. Language PARSER   → shared IRNode[]  (universal IR)
 *    3. Four CODE GENs    → Python / C / C++ / Java strings
 *    4. JSON serialiser   → stdout
 *
 *  FIXES vs v2.0:
 *    BUG-1: Template types like vector<int> now produce a proper
 *           [PARSER] error instead of silently being skipped.
 *           Any unrecognised identifier statement also now reports
 *           an error rather than silently calling skipLine().
 *    BUG-2: genPython / genC / genCpp / genJava no longer append
 *           a hard-coded "return 0" when the IR already contains
 *           an IR_RETURN node.  Python output never emits
 *           "return 0" from the footer at all (it is not needed).
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── LIMITS ─────────────────────────────────────────── */
#define MAX_SOURCE     8192
#define MAX_TOKEN_LEN    80
#define MAX_TOKENS      800
#define MAX_IR_NODES    512
#define MAX_NAME_LEN     64
#define MAX_ERRORS       64
#define MAX_ERROR_LEN   200
#define MAX_OUTPUT    16384

/* ═══════════════════════════════════════════════════════
   SECTION 1 — TOKEN TYPES
═══════════════════════════════════════════════════════ */
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
    /* keywords */
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR,
    TOK_RETURN, TOK_PRINT,
    TOK_INT, TOK_FLOAT_KW, TOK_CHAR_KW, TOK_VOID, TOK_AUTO,
    TOK_BOOL, TOK_STRING_KW,
    TOK_PUBLIC, TOK_CLASS, TOK_STATIC,
    TOK_INCLUDE, TOK_USING, TOK_NAMESPACE,
    TOK_ENDL, TOK_CIN, TOK_COUT,
    TOK_TRUE, TOK_FALSE,
    TOK_NEWLINE, TOK_EOF, TOK_UNKNOWN
} TokenType;

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
    "PUBLIC","CLASS","STATIC",
    "INCLUDE","USING","NAMESPACE","ENDL","CIN","COUT",
    "TRUE","FALSE",
    "NEWLINE","EOF","UNKNOWN"
};

typedef struct { TokenType type; char lexeme[MAX_TOKEN_LEN]; } Token;

/* ═══════════════════════════════════════════════════════
   SECTION 2 — INTERMEDIATE REPRESENTATION (IR)
   ALL input languages compile to this universal IR.
═══════════════════════════════════════════════════════ */
typedef enum {
    IR_ASSIGN,      /* dest = src1                           */
    IR_BINOP,       /* dest = src1 op src2                   */
    IR_PRINT,       /* print(dest)                           */
    IR_IF,          /* if (src1 op src2)                     */
    IR_ELSE,        /* else                                  */
    IR_WHILE,       /* while (src1 op src2)                  */
    IR_FOR,         /* for dest = src1; dest op src2; dest++ */
    IR_RETURN,      /* return src1                           */
    IR_BLOCK_START, /* {                                     */
    IR_BLOCK_END,   /* }                                     */
    IR_UNKNOWN
} IRType;

const char *IR_NAMES[] = {
    "IR_ASSIGN","IR_BINOP","IR_PRINT",
    "IR_IF","IR_ELSE","IR_WHILE","IR_FOR","IR_RETURN",
    "IR_BLOCK_START","IR_BLOCK_END","IR_UNKNOWN"
};

typedef struct {
    IRType type;
    char dest[MAX_NAME_LEN];  /* result variable / loop var        */
    char src1[MAX_NAME_LEN];  /* first operand / loop start value  */
    char op[8];               /* operator                          */
    char src2[MAX_NAME_LEN];  /* second operand / loop end value   */
    char dtype[16];           /* declared type hint: int/auto/...  */
} IRNode;

/* ── GLOBAL STATE ───────────────────────────────────── */
Token  toks[MAX_TOKENS];
int    tokCount = 0;

IRNode ir[MAX_IR_NODES];
int    irCount = 0;

char   errs[MAX_ERRORS][MAX_ERROR_LEN];
int    errCount = 0;

char   srcLang[16] = "custom";

/* ── UTILITIES ──────────────────────────────────────── */
void addErr(const char *m) {
    if (errCount < MAX_ERRORS)
        strncpy(errs[errCount++], m, MAX_ERROR_LEN-1);
}

void jsonEsc(const char *s, char *out, int len) {
    int j=0;
    for (int i=0; s[i] && j<len-2; i++) {
        char c=s[i];
        if      (c=='"')  { out[j++]='\\'; out[j++]='"';  }
        else if (c=='\\') { out[j++]='\\'; out[j++]='\\'; }
        else if (c=='\n') { out[j++]='\\'; out[j++]='n';  }
        else if (c=='\r') { out[j++]='\\'; out[j++]='r';  }
        else if (c=='\t') { out[j++]='\\'; out[j++]='t';  }
        else              { out[j++]=c; }
    }
    out[j]='\0';
}

void app(char *buf, const char *s, int sz) {
    int cur=(int)strlen(buf), rem=sz-cur-1;
    if (rem>0) strncat(buf,s,rem);
}

/* ── Helper: does the IR contain at least one IR_RETURN? ── */
static int irHasReturn(void) {
    for (int i = 0; i < irCount; i++)
        if (ir[i].type == IR_RETURN) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════
   SECTION 3 — UNIVERSAL LEXER
   One lexer handles all 4 input languages.
   Language-specific keywords resolved in identOrKeyword().
═══════════════════════════════════════════════════════ */
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
    if (!strcmp(w,"class"))     return TOK_CLASS;
    if (!strcmp(w,"static"))    return TOK_STATIC;
    return TOK_IDENT;
}

void lexAll(const char *src) {
    int i=0, n=(int)strlen(src);
    tokCount=0;

    while (i<n && tokCount<MAX_TOKENS-1) {
        char c=src[i];

        /* skip C/C++ line comments */
        if (c=='/' && i+1<n && src[i+1]=='/') {
            while (i<n && src[i]!='\n') i++; continue;
        }
        /* skip block comments */
        if (c=='/' && i+1<n && src[i+1]=='*') {
            i+=2;
            while (i+1<n && !(src[i]=='*' && src[i+1]=='/')) i++;
            i+=2; continue;
        }
        /* Python/custom # comments — treated as line comment */
        if (c=='#') {
            while (i<n && src[i]!='\n') i++; continue;
        }

        if (c==' '||c=='\t'||c=='\r') { i++; continue; }

        if (c=='\n') {
            toks[tokCount].type=TOK_NEWLINE;
            strcpy(toks[tokCount].lexeme,"\\n");
            tokCount++; i++; continue;
        }

        /* string literals */
        if (c=='"'||c=='\'') {
            char q=c; i++;
            int s=i;
            while (i<n && src[i]!=q) { if(src[i]=='\\') i++; i++; }
            int sl=i-s; if(sl>=MAX_TOKEN_LEN-2) sl=MAX_TOKEN_LEN-3;
            toks[tokCount].type=TOK_STRING;
            toks[tokCount].lexeme[0]=q;
            strncpy(toks[tokCount].lexeme+1, src+s, sl);
            toks[tokCount].lexeme[sl+1]=q;
            toks[tokCount].lexeme[sl+2]='\0';
            tokCount++; if(i<n) i++; continue;
        }

        /* identifiers / keywords */
        if (isalpha(c)||c=='_') {
            int s=i;
            while (i<n&&(isalnum(src[i])||src[i]=='_')) i++;
            int wl=i-s; if(wl>=MAX_TOKEN_LEN) wl=MAX_TOKEN_LEN-1;
            strncpy(toks[tokCount].lexeme, src+s, wl);
            toks[tokCount].lexeme[wl]='\0';
            toks[tokCount].type=identOrKeyword(toks[tokCount].lexeme);
            tokCount++; continue;
        }

        /* numbers (int + float) */
        if (isdigit(c)||(c=='.'&&i+1<n&&isdigit(src[i+1]))) {
            int s=i;
            while (i<n&&(isdigit(src[i])||src[i]=='.')) i++;
            int nl=i-s; if(nl>=MAX_TOKEN_LEN) nl=MAX_TOKEN_LEN-1;
            strncpy(toks[tokCount].lexeme, src+s, nl);
            toks[tokCount].lexeme[nl]='\0';
            toks[tokCount].type=TOK_NUMBER;
            tokCount++; continue;
        }

/* Helper macro: match two-char operator */
#define TOK2(a,b,t,l) if(c==a&&i+1<n&&src[i+1]==b){toks[tokCount].type=t;strcpy(toks[tokCount].lexeme,l);tokCount++;i+=2;continue;}
        TOK2('=','=',TOK_EQ,"==")
        TOK2('!','=',TOK_NEQ,"!=")
        TOK2('>','=',TOK_GTE,">=")
        TOK2('<','=',TOK_LTE,"<=")
        TOK2('&','&',TOK_AND,"&&")
        TOK2('|','|',TOK_OR,"||")
        TOK2('<','<',TOK_LSHIFT,"<<")
        TOK2('>','>',TOK_RSHIFT,">>")
        TOK2('-','>',TOK_ARROW,"->")
        TOK2(':',':',TOK_DCOLON,"::")

        /* single-char tokens */
        {
            TokenType st=TOK_UNKNOWN; char sl[3]={c,'\0','\0'};
            switch(c){
                case '=': st=TOK_ASSIGN;   break;
                case '+': st=TOK_PLUS;     break;
                case '-': st=TOK_MINUS;    break;
                case '*': st=TOK_STAR;     break;
                case '/': st=TOK_SLASH;    break;
                case '%': st=TOK_PERCENT;  break;
                case '>': st=TOK_GT;       break;
                case '<': st=TOK_LT;       break;
                case '(': st=TOK_LPAREN;   break;
                case ')': st=TOK_RPAREN;   break;
                case '{': st=TOK_LBRACE;   break;
                case '}': st=TOK_RBRACE;   break;
                case '[': st=TOK_LBRACKET; break;
                case ']': st=TOK_RBRACKET; break;
                case ';': st=TOK_SEMICOLON;break;
                case ':': st=TOK_COLON;    break;
                case ',': st=TOK_COMMA;    break;
                case '.': st=TOK_DOT;      break;
                case '&': st=TOK_AMP;      break;
                case '|': st=TOK_PIPE;     break;
                case '!': st=TOK_NOT;      break;
                case '#': st=TOK_HASH;     break;
                default:  { char msg[64]; snprintf(msg,64,"[LEXER] Unknown char '%c'",c); addErr(msg); i++; continue; }
            }
            toks[tokCount].type=st;
            strcpy(toks[tokCount].lexeme,sl);
            tokCount++; i++;
        }
    }

    toks[tokCount].type=TOK_EOF;
    strcpy(toks[tokCount].lexeme,"EOF");
    tokCount++;
}

/* ═══════════════════════════════════════════════════════
   SECTION 4 — PARSERS
   Four front-ends, one shared IR output.
═══════════════════════════════════════════════════════ */
int pos=0;

static inline Token cur()  { return (pos<tokCount)?toks[pos]:toks[tokCount-1]; }
static inline Token adv()  { Token t=cur(); if(pos<tokCount-1) pos++; return t; }
static inline void skipNL(){ while(cur().type==TOK_NEWLINE) adv(); }
static inline void skipSC(){ while(cur().type==TOK_SEMICOLON||cur().type==TOK_NEWLINE) adv(); }

static inline int isTypeKw(TokenType t){
    return t==TOK_INT||t==TOK_FLOAT_KW||t==TOK_CHAR_KW||t==TOK_VOID||
           t==TOK_AUTO||t==TOK_BOOL||t==TOK_STRING_KW;
}
static inline int isArith(TokenType t){
    return t==TOK_PLUS||t==TOK_MINUS||t==TOK_STAR||t==TOK_SLASH||t==TOK_PERCENT;
}
static inline int isCmp(TokenType t){
    return t==TOK_GT||t==TOK_LT||t==TOK_EQ||t==TOK_NEQ||t==TOK_GTE||t==TOK_LTE;
}

static void pushIR(IRType tp, const char *dest, const char *s1,
                   const char *op, const char *s2, const char *dt){
    if(irCount>=MAX_IR_NODES) return;
    IRNode nd; memset(&nd,0,sizeof(nd));
    nd.type=tp;
    if(dest) strncpy(nd.dest,dest,MAX_NAME_LEN-1);
    if(s1)   strncpy(nd.src1,s1,  MAX_NAME_LEN-1);
    if(op)   strncpy(nd.op,  op,  7);
    if(s2)   strncpy(nd.src2,s2,  MAX_NAME_LEN-1);
    if(dt)   strncpy(nd.dtype,dt, 15);
    ir[irCount++]=nd;
}

/* Read a simple operand (ident, number, -number, or ident[idx]) */
static Token readOperand(){
    skipNL();
    if(cur().type==TOK_IDENT||cur().type==TOK_NUMBER||
       cur().type==TOK_TRUE||cur().type==TOK_FALSE){
        Token t=adv();
        /* handle subscript: y[0] → keep as "y[0]" in lexeme */
        if(cur().type==TOK_LBRACKET){
            adv(); /* eat [ */
            char idx[MAX_TOKEN_LEN]="0";
            if(cur().type==TOK_NUMBER||cur().type==TOK_IDENT){
                strncpy(idx,cur().lexeme,MAX_TOKEN_LEN-1); adv();
            }
            if(cur().type==TOK_RBRACKET) adv(); /* eat ] */
            char combined[MAX_TOKEN_LEN];
            snprintf(combined,MAX_TOKEN_LEN,"%s[%s]",t.lexeme,idx);
            strncpy(t.lexeme,combined,MAX_TOKEN_LEN-1);
        }
        return t;
    }
    if(cur().type==TOK_MINUS){
        adv(); Token n=adv();
        Token r; snprintf(r.lexeme,MAX_TOKEN_LEN,"-%s",n.lexeme); r.type=TOK_NUMBER; return r;
    }
    Token bad; strcpy(bad.lexeme,"0"); bad.type=TOK_NUMBER; return bad;
}

/* Skip to end of current line/statement */
static void skipLine(){
    while(cur().type!=TOK_NEWLINE&&cur().type!=TOK_SEMICOLON&&
          cur().type!=TOK_RBRACE &&cur().type!=TOK_EOF) adv();
}

/* ── SHARED STATEMENT PARSER (used by C / C++ / Java body) ── */
void sharedStmt() {
    skipNL();
    if(cur().type==TOK_EOF) return;

    /* { */
    if(cur().type==TOK_LBRACE){ adv(); pushIR(IR_BLOCK_START,"","","","",""); return; }
    /* } */
    if(cur().type==TOK_RBRACE){ adv(); pushIR(IR_BLOCK_END,  "","","","",""); return; }

    /* return x; */
    if(cur().type==TOK_RETURN){
        adv(); Token v=readOperand(); skipSC();
        pushIR(IR_RETURN,"",v.lexeme,"","",""); return;
    }

    /* if (cond) */
    if(cur().type==TOK_IF){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if(cur().type==TOK_RPAREN) adv();
        pushIR(IR_IF,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    /* else */
    if(cur().type==TOK_ELSE){
        adv(); pushIR(IR_ELSE,"","","","","");
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    /* while (cond) */
    if(cur().type==TOK_WHILE){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if(cur().type==TOK_RPAREN) adv();
        pushIR(IR_WHILE,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    /* for (int i=0; i<n; i++) */
    if(cur().type==TOK_FOR){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        char dtype[16]="int";
        if(isTypeKw(cur().type)) strncpy(dtype,adv().lexeme,15);
        Token ivar=adv();             /* loop variable  */
        if(cur().type==TOK_ASSIGN) adv();
        Token istart=readOperand();   /* start value    */
        skipSC();                     /* ; */
        Token cl=readOperand();       /* condition lhs  */
        Token cop=adv();              /* < > <= >= == */
        Token cr=readOperand();       /* condition rhs  */
        skipSC();                     /* ; */
        /* skip increment expression */
        while(cur().type!=TOK_RPAREN&&cur().type!=TOK_EOF) adv();
        if(cur().type==TOK_RPAREN) adv();
        (void)cl;
        pushIR(IR_FOR,ivar.lexeme,istart.lexeme,cop.lexeme,cr.lexeme,dtype);
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    /* printf("fmt", x) */
    if(cur().type==TOK_IDENT && !strcmp(cur().lexeme,"printf")){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        if(cur().type==TOK_STRING) adv(); /* skip format string */
        if(cur().type==TOK_COMMA)  adv();
        Token v=readOperand();
        while(cur().type!=TOK_RPAREN&&cur().type!=TOK_SEMICOLON&&cur().type!=TOK_EOF) adv();
        if(cur().type==TOK_RPAREN) adv();
        if(strcmp(v.lexeme,"0")) pushIR(IR_PRINT,v.lexeme,"","","","");
        return;
    }

    /* cout << x << endl */
    if(cur().type==TOK_COUT){
        adv();
        while(cur().type==TOK_LSHIFT){
            adv();
            if(cur().type==TOK_ENDL||cur().type==TOK_NEWLINE||cur().type==TOK_SEMICOLON){
                if(cur().type==TOK_ENDL) adv(); break;
            }
            Token v=readOperand();
            if(strcmp(v.lexeme,"0")) pushIR(IR_PRINT,v.lexeme,"","","","");
        }
        return;
    }

    /* System.out.println(x) */
    if(cur().type==TOK_IDENT && !strcmp(cur().lexeme,"System")){
        adv(); /* System */
        while(cur().type==TOK_DOT){adv(); adv();} /* .out.println */
        if(cur().type==TOK_LPAREN) adv();
        Token v=readOperand();
        if(cur().type==TOK_RPAREN) adv();
        if(strcmp(v.lexeme,"0")) pushIR(IR_PRINT,v.lexeme,"","","","");
        return;
    }

    /* type? name = expr ; */
    char dtype[16]="";
    if(isTypeKw(cur().type)) strncpy(dtype,adv().lexeme,15);

    if(cur().type==TOK_IDENT){
        Token dest=adv();

        /* ── Template type handling: vector<int>, map<K,V>, list<T> etc. ──
           Strategy: extract the inner (element) type from <…>, use it as
           the dtype for the variable, skip any {…} initialiser brace-list,
           then parse the variable name and value normally.
           A [WARN] note is added so the user knows the type was simplified,
           but transpilation continues — no hard error. */
        if(cur().type==TOK_LT){
            char tplName[MAX_NAME_LEN];
            strncpy(tplName, dest.lexeme, MAX_NAME_LEN-1);   /* e.g. "vector" */

            /* consume everything inside < … > — may nest (map<K,V>) */
            adv(); /* eat '<' */
            char innerType[16]="auto";
            int depth=1, first=1;
            while(cur().type!=TOK_EOF && depth>0){
                if(cur().type==TOK_LT){ depth++; adv(); continue; }
                if(cur().type==TOK_GT){ depth--; adv(); continue; }
                if(cur().type==TOK_RSHIFT){ depth-=2; adv(); continue; } /* >> closes two */
                /* capture first token inside <> as inner type */
                if(first && depth==1 && isTypeKw(cur().type)){
                    strncpy(innerType, cur().lexeme, 15);
                    first=0;
                }
                adv();
            }
            /* after '>' the variable name follows — e.g. "y" in vector<int> y */
            skipNL();
            if(cur().type!=TOK_IDENT){
                /* malformed — just skip the line */
                skipLine(); return;
            }
            Token varName = adv();   /* the actual variable name */

            /* skip optional = { … } or = value initialiser */
            if(cur().type==TOK_ASSIGN){
                adv();
                if(cur().type==TOK_LBRACE){
                    /* brace-list initialiser: skip { … } */
                    adv(); int bd=1;
                    while(cur().type!=TOK_EOF && bd>0){
                        if(cur().type==TOK_LBRACE) bd++;
                        else if(cur().type==TOK_RBRACE) bd--;
                        adv();
                    }
                    /* emit assign with "0" as placeholder scalar */
                    char warnMsg[256];
                    snprintf(warnMsg,256,
                        "[WARN] '%s<%s>' simplified to '%s' — "
                        "brace initialiser replaced with 0",
                        tplName, innerType, innerType);
                    addErr(warnMsg);
                    pushIR(IR_ASSIGN, varName.lexeme, "0", "", "", innerType);
                } else {
                    /* scalar initialiser: vector<int> x = 10 */
                    Token s1 = readOperand();
                    char warnMsg[256];
                    snprintf(warnMsg,256,
                        "[WARN] '%s<%s>' simplified to scalar '%s'",
                        tplName, innerType, innerType);
                    addErr(warnMsg);
                    if(isArith(cur().type)){
                        Token op=adv(), s2=readOperand();
                        pushIR(IR_BINOP,varName.lexeme,s1.lexeme,op.lexeme,s2.lexeme,innerType);
                    } else {
                        pushIR(IR_ASSIGN, varName.lexeme, s1.lexeme, "", "", innerType);
                    }
                }
            } else {
                /* declaration with no initialiser: vector<int> x; → x = 0 */
                char warnMsg[256];
                snprintf(warnMsg,256,
                    "[WARN] '%s<%s>' simplified to '%s', initialised to 0",
                    tplName, innerType, innerType);
                addErr(warnMsg);
                pushIR(IR_ASSIGN, varName.lexeme, "0", "", "", innerType);
            }
            skipSC();
            return;
        }

        if(cur().type==TOK_ASSIGN){
            adv();
            Token s1=readOperand();
            if(isArith(cur().type)){
                Token op=adv(), s2=readOperand();
                pushIR(IR_BINOP,dest.lexeme,s1.lexeme,op.lexeme,s2.lexeme,dtype);
            } else {
                pushIR(IR_ASSIGN,dest.lexeme,s1.lexeme,"","",dtype);
            }
            skipSC(); return;
        }
        /* compound assignment +=, -=, etc. — simplify to BINOP */
        if(cur().type==TOK_PLUS||cur().type==TOK_MINUS||
           cur().type==TOK_STAR||cur().type==TOK_SLASH){
            char op2[4]; strncpy(op2,cur().lexeme,3); adv();
            if(cur().type==TOK_ASSIGN){ adv();
                Token s2=readOperand();
                pushIR(IR_BINOP,dest.lexeme,dest.lexeme,op2,s2.lexeme,dtype);
                skipSC(); return;
            }
        }

        /* ── FIX BUG-1b: unrecognised statement — was silently skipped ──
           Now we emit a proper parser error so the UI shows it in the
           errors panel instead of pretending nothing happened. */
        char msg[256];
        snprintf(msg,256,
            "[PARSER] Unexpected token '%s' after identifier '%s': "
            "statement not recognised",
            cur().lexeme, dest.lexeme);
        addErr(msg);
    }

    skipLine();
}

/* ── PARSER: Python / Custom ── */
void parsePyStmt();
void parsePython(){
    pos=0; irCount=0;
    while(cur().type!=TOK_EOF){
        skipNL();
        if(cur().type==TOK_EOF) break;
        parsePyStmt();
        skipSC();
    }
}
void parsePyStmt(){
    skipNL();
    if(cur().type==TOK_EOF) return;

    /* print x  or  print(x) */
    if(cur().type==TOK_PRINT){
        adv();
        int paren=(cur().type==TOK_LPAREN); if(paren) adv();
        Token v=readOperand();
        if(paren&&cur().type==TOK_RPAREN) adv();
        pushIR(IR_PRINT,v.lexeme,"","","",""); return;
    }

    /* if x > 5 : */
    if(cur().type==TOK_IF){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if(cur().type==TOK_RPAREN) adv();
        if(cur().type==TOK_COLON) adv();
        pushIR(IR_IF,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    /* else : */
    if(cur().type==TOK_ELSE){
        adv(); if(cur().type==TOK_COLON) adv();
        pushIR(IR_ELSE,"","","","",""); return;
    }

    /* while x < 10 : */
    if(cur().type==TOK_WHILE){
        adv();
        if(cur().type==TOK_LPAREN) adv();
        Token l=readOperand(), op=adv(), r=readOperand();
        if(cur().type==TOK_RPAREN) adv();
        if(cur().type==TOK_COLON) adv();
        pushIR(IR_WHILE,"",l.lexeme,op.lexeme,r.lexeme,"");
        skipNL();
        if(cur().type==TOK_LBRACE){adv();pushIR(IR_BLOCK_START,"","","","","");}
        return;
    }

    if(cur().type==TOK_RBRACE){adv(); pushIR(IR_BLOCK_END,"","","","",""); return;}
    if(cur().type==TOK_RETURN){
        adv(); Token v=readOperand();
        pushIR(IR_RETURN,"",v.lexeme,"","",""); return;
    }

    /* optional type hint then  name = expr */
    char dtype[16]="";
    if(isTypeKw(cur().type)) strncpy(dtype,adv().lexeme,15);

    if(cur().type==TOK_IDENT){
        Token dest=adv();
        if(cur().type==TOK_ASSIGN){
            adv();
            Token s1=readOperand();
            if(isArith(cur().type)){
                Token op=adv(), s2=readOperand();
                pushIR(IR_BINOP,dest.lexeme,s1.lexeme,op.lexeme,s2.lexeme,dtype);
            } else {
                pushIR(IR_ASSIGN,dest.lexeme,s1.lexeme,"","",dtype);
            }
            return;
        }
    }
    skipLine();
}

/* ── PARSER: C ── */
void parseC(){
    pos=0; irCount=0;
    while(cur().type!=TOK_EOF){
        skipNL();
        if(cur().type==TOK_EOF) break;
        /* skip preprocessor lines */
        if(cur().type==TOK_HASH||cur().type==TOK_INCLUDE){
            while(cur().type!=TOK_NEWLINE&&cur().type!=TOK_EOF) adv(); continue;
        }
        /* skip: int main(...) { — we parse the body */
        if((cur().type==TOK_INT||cur().type==TOK_VOID)){
            Token saved=adv();
            if(cur().type==TOK_IDENT&&!strcmp(cur().lexeme,"main")){
                while(cur().type!=TOK_LBRACE&&cur().type!=TOK_EOF) adv();
                if(cur().type==TOK_LBRACE) adv();
                continue;
            }
            /* variable declaration — reconstruct */
            char dtype[16]; strncpy(dtype,saved.lexeme,15);
            if(cur().type==TOK_IDENT){
                Token dest=adv();
                if(cur().type==TOK_ASSIGN){
                    adv();
                    Token s1=readOperand();
                    if(isArith(cur().type)){
                        Token op=adv(),s2=readOperand();
                        pushIR(IR_BINOP,dest.lexeme,s1.lexeme,op.lexeme,s2.lexeme,dtype);
                    } else {
                        pushIR(IR_ASSIGN,dest.lexeme,s1.lexeme,"","",dtype);
                    }
                }
            }
            skipSC(); continue;
        }
        sharedStmt();
        skipSC();
    }
}

/* ── PARSER: C++ ── */
void parseCpp(){
    pos=0; irCount=0;
    while(cur().type!=TOK_EOF){
        skipNL();
        if(cur().type==TOK_EOF) break;
        /* skip preprocessor / using */
        if(cur().type==TOK_HASH||cur().type==TOK_INCLUDE){
            while(cur().type!=TOK_NEWLINE&&cur().type!=TOK_EOF) adv(); continue;
        }
        if(cur().type==TOK_USING){
            while(cur().type!=TOK_SEMICOLON&&cur().type!=TOK_NEWLINE&&cur().type!=TOK_EOF) adv();
            skipSC(); continue;
        }
        /* skip int/void main() { */
        if(cur().type==TOK_INT||cur().type==TOK_VOID){
            Token saved=adv();
            if(cur().type==TOK_IDENT&&!strcmp(cur().lexeme,"main")){
                while(cur().type!=TOK_LBRACE&&cur().type!=TOK_EOF) adv();
                if(cur().type==TOK_LBRACE) adv();
                continue;
            }
            char dtype[16]; strncpy(dtype,saved.lexeme,15);
            if(cur().type==TOK_IDENT){
                Token dest=adv();
                if(cur().type==TOK_ASSIGN){
                    adv();
                    Token s1=readOperand();
                    if(isArith(cur().type)){
                        Token op=adv(),s2=readOperand();
                        pushIR(IR_BINOP,dest.lexeme,s1.lexeme,op.lexeme,s2.lexeme,dtype);
                    } else {
                        pushIR(IR_ASSIGN,dest.lexeme,s1.lexeme,"","",dtype);
                    }
                }
            }
            skipSC(); continue;
        }
        sharedStmt();
        skipSC();
    }
}

/* ── PARSER: Java ── */
void parseJava(){
    pos=0; irCount=0;
    /* skip to inside main() body */
    int braceDepth=0;
    while(cur().type!=TOK_EOF){
        if(cur().type==TOK_IDENT&&!strcmp(cur().lexeme,"main")){
            while(cur().type!=TOK_LBRACE&&cur().type!=TOK_EOF) adv();
            if(cur().type==TOK_LBRACE){adv(); braceDepth++; break;}
        }
        adv();
    }
    /* parse body */
    while(cur().type!=TOK_EOF){
        skipNL();
        if(cur().type==TOK_EOF) break;
        if(cur().type==TOK_RBRACE){
            braceDepth--;
            if(braceDepth<=0){adv();break;}
        }
        sharedStmt();
        skipSC();
    }
}

/* ═══════════════════════════════════════════════════════
   SECTION 5 — CODE GENERATORS
   All four generators walk the same IR array.
═══════════════════════════════════════════════════════ */

static void indent(char *buf, int depth, int sz){
    char s[80]=""; int sp=depth*4; if(sp>76)sp=76;
    for(int i=0;i<sp;i++) s[i]=' ';
    s[sp]='\0'; app(buf,s,sz);
}

/* ── Python output ── */
void genPython(char *buf, int sz){
    buf[0]='\0';
    app(buf,"# Generated by Mini-Transpiler\n\n",sz);
    int depth=0; char line[512];
    for(int i=0;i<irCount;i++){
        IRNode *n=&ir[i];
        /* BLOCK_START/END only adjust depth — Python uses indentation */
        if(n->type==IR_BLOCK_START){ continue; }
        if(n->type==IR_BLOCK_END)  { if(depth>0)depth--; continue; }
        if(n->type==IR_ELSE){
            if(depth>0)depth--;
            indent(buf,depth,sz);
            app(buf,"else:\n",sz);
            depth++; continue;
        }
        indent(buf,depth,sz);
        if     (n->type==IR_ASSIGN) snprintf(line,512,"%s = %s\n",n->dest,n->src1);
        else if(n->type==IR_BINOP)  snprintf(line,512,"%s = %s %s %s\n",n->dest,n->src1,n->op,n->src2);
        else if(n->type==IR_PRINT)  snprintf(line,512,"print(%s)\n",n->dest);
        /* FIX BUG-2 (Python): skip bare "return 0" that comes from a C/C++
           main() — Python scripts at module level must not have "return".
           Only emit return if we are inside an indented block (depth > 0). */
        else if(n->type==IR_RETURN){
            if(depth > 0) snprintf(line,512,"return %s\n",n->src1);
            else continue;
        }
        else if(n->type==IR_IF){
            snprintf(line,512,"if %s %s %s:\n",n->src1,n->op,n->src2);
            app(buf,line,sz); depth++; continue;
        }
        else if(n->type==IR_WHILE){
            snprintf(line,512,"while %s %s %s:\n",n->src1,n->op,n->src2);
            app(buf,line,sz); depth++; continue;
        }
        else if(n->type==IR_FOR){
            snprintf(line,512,"for %s in range(%s, %s):\n",n->dest,n->src1,n->src2);
            app(buf,line,sz); depth++; continue;
        }
        else continue;
        app(buf,line,sz);
    }
    /* Python scripts do not need "return 0" — nothing appended here. */
}

/* ── C output ── */
void genC(char *buf, int sz){
    buf[0]='\0';
    app(buf,"/* Generated by Mini-Transpiler */\n#include <stdio.h>\n\nint main() {\n",sz);
    int depth=1; char line[512]; char tp[20];
    for(int i=0;i<irCount;i++){
        IRNode *n=&ir[i];
        if(n->type==IR_BLOCK_START){ depth++; continue; }
        if(n->type==IR_BLOCK_END)  { if(depth>1)depth--; indent(buf,depth,sz); app(buf,"}\n",sz); continue; }
        if(n->type==IR_ELSE)       { if(depth>1)depth--; indent(buf,depth-1,sz); app(buf,"} else {\n",sz); depth++; continue; }
        indent(buf,depth,sz);
        strncpy(tp,n->dtype[0]?n->dtype:"int",19);
        if(!strcmp(tp,"auto")) strcpy(tp,"int");
        if(!strcmp(tp,"String")) strcpy(tp,"char*");
        if     (n->type==IR_ASSIGN) snprintf(line,512,"%s %s = %s;\n",tp,n->dest,n->src1);
        else if(n->type==IR_BINOP)  snprintf(line,512,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2);
        else if(n->type==IR_PRINT)  snprintf(line,512,"printf(\"%%d\\n\", %s);\n",n->dest);
        else if(n->type==IR_IF)     { snprintf(line,512,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_WHILE)  { snprintf(line,512,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_FOR)    { snprintf(line,512,"for (int %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_RETURN) snprintf(line,512,"return %s;\n",n->src1);
        else continue;
        app(buf,line,sz);
    }
    while(depth>1){ depth--; indent(buf,depth-1,sz); app(buf,"}\n",sz); }
    /* FIX BUG-2 (C): only add "return 0" if the source had no return statement */
    if(!irHasReturn())
        app(buf,"    return 0;\n}\n",sz);
    else
        app(buf,"}\n",sz);
}

/* ── C++ output ── */
void genCpp(char *buf, int sz){
    buf[0]='\0';
    app(buf,"// Generated by Mini-Transpiler\n#include <iostream>\nusing namespace std;\n\nint main() {\n",sz);
    int depth=1; char line[512]; char tp[20];
    for(int i=0;i<irCount;i++){
        IRNode *n=&ir[i];
        if(n->type==IR_BLOCK_START){ depth++; continue; }
        if(n->type==IR_BLOCK_END)  { if(depth>1)depth--; indent(buf,depth,sz); app(buf,"}\n",sz); continue; }
        if(n->type==IR_ELSE)       { if(depth>1)depth--; indent(buf,depth-1,sz); app(buf,"} else {\n",sz); depth++; continue; }
        indent(buf,depth,sz);
        strncpy(tp,n->dtype[0]?n->dtype:"auto",19);
        if     (n->type==IR_ASSIGN) snprintf(line,512,"%s %s = %s;\n",tp,n->dest,n->src1);
        else if(n->type==IR_BINOP)  snprintf(line,512,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2);
        else if(n->type==IR_PRINT)  snprintf(line,512,"cout << %s << endl;\n",n->dest);
        else if(n->type==IR_IF)     { snprintf(line,512,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_WHILE)  { snprintf(line,512,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_FOR)    { snprintf(line,512,"for (auto %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_RETURN) snprintf(line,512,"return %s;\n",n->src1);
        else continue;
        app(buf,line,sz);
    }
    while(depth>1){ depth--; indent(buf,depth-1,sz); app(buf,"}\n",sz); }
    /* FIX BUG-2 (C++): only add "return 0" if no return was in the source */
    if(!irHasReturn())
        app(buf,"    return 0;\n}\n",sz);
    else
        app(buf,"}\n",sz);
}

/* ── Java output ── */
void genJava(char *buf, int sz){
    buf[0]='\0';
    app(buf,"// Generated by Mini-Transpiler\npublic class Output {\n    public static void main(String[] args) {\n",sz);
    int depth=2; char line[512]; char tp[20];
    for(int i=0;i<irCount;i++){
        IRNode *n=&ir[i];
        if(n->type==IR_BLOCK_START){ depth++; continue; }
        if(n->type==IR_BLOCK_END)  { if(depth>2)depth--; indent(buf,depth,sz); app(buf,"}\n",sz); continue; }
        if(n->type==IR_ELSE)       { if(depth>2)depth--; indent(buf,depth-1,sz); app(buf,"} else {\n",sz); depth++; continue; }
        indent(buf,depth,sz);
        strncpy(tp,n->dtype[0]?n->dtype:"int",19);
        if(!strcmp(tp,"auto")) strcpy(tp,"int");
        if     (n->type==IR_ASSIGN) snprintf(line,512,"%s %s = %s;\n",tp,n->dest,n->src1);
        else if(n->type==IR_BINOP)  snprintf(line,512,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2);
        else if(n->type==IR_PRINT)  snprintf(line,512,"System.out.println(%s);\n",n->dest);
        else if(n->type==IR_IF)     { snprintf(line,512,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_WHILE)  { snprintf(line,512,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,sz); depth++; continue; }
        else if(n->type==IR_FOR)    { snprintf(line,512,"for (int %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,sz); depth++; continue; }
        /* FIX BUG-2 (Java): suppress "return 0" — Java main() is void-returning
           in practice; return 0 is meaningless and was never in the source intent. */
        else if(n->type==IR_RETURN){
            if(!strcmp(n->src1,"0")) continue; /* skip return 0 */
            snprintf(line,512,"return %s;\n",n->src1);
        }
        else continue;
        app(buf,line,sz);
    }
    while(depth>2){ depth--; indent(buf,depth-1,sz); app(buf,"}\n",sz); }
    /* FIX BUG-2 (Java): only append closing braces; no hard-coded return 0 */
    app(buf,"    }\n}\n",sz);
}

/* ═══════════════════════════════════════════════════════
   SECTION 6 — JSON OUTPUT
═══════════════════════════════════════════════════════ */
void outputJSON(){
    static char py[MAX_OUTPUT], cb[MAX_OUTPUT];
    static char cp[MAX_OUTPUT], jv[MAX_OUTPUT];
    static char esc[MAX_OUTPUT*2];

    genPython(py,MAX_OUTPUT);
    genC     (cb,MAX_OUTPUT);
    genCpp   (cp,MAX_OUTPUT);
    genJava  (jv,MAX_OUTPUT);

    printf("{");

    /* tokens */
    printf("\"tokens\":[");
    int first=1;
    for(int i=0;i<tokCount;i++){
        if(toks[i].type==TOK_NEWLINE||toks[i].type==TOK_EOF) continue;
        char e2[MAX_TOKEN_LEN*2]; jsonEsc(toks[i].lexeme,e2,sizeof(e2));
        if(!first) printf(",");
        printf("{\"type\":\"%s\",\"lexeme\":\"%s\"}",TOK_NAMES[toks[i].type],e2);
        first=0;
    }
    printf("],");

    /* ir */
    printf("\"ir\":[");
    for(int i=0;i<irCount;i++){
        char d[MAX_NAME_LEN*2],s1[MAX_NAME_LEN*2],op2[16],s2[MAX_NAME_LEN*2],dt[32];
        jsonEsc(ir[i].dest, d, sizeof(d));
        jsonEsc(ir[i].src1, s1,sizeof(s1));
        jsonEsc(ir[i].op,   op2,sizeof(op2));
        jsonEsc(ir[i].src2, s2, sizeof(s2));
        jsonEsc(ir[i].dtype,dt, sizeof(dt));
        if(i) printf(",");
        printf("{\"type\":\"%s\",\"dest\":\"%s\",\"src1\":\"%s\",\"op\":\"%s\",\"src2\":\"%s\",\"dtype\":\"%s\"}",
               IR_NAMES[ir[i].type],d,s1,op2,s2,dt);
    }
    printf("],");

    jsonEsc(py,esc,sizeof(esc)); printf("\"python\":\"%s\",",esc);
    jsonEsc(cb,esc,sizeof(esc)); printf("\"c\":\"%s\",",esc);
    jsonEsc(cp,esc,sizeof(esc)); printf("\"cpp\":\"%s\",",esc);
    jsonEsc(jv,esc,sizeof(esc)); printf("\"java\":\"%s\",",esc);

    printf("\"srclang\":\"%s\",",srcLang);

    printf("\"errors\":[");
    for(int i=0;i<errCount;i++){
        char e2[MAX_ERROR_LEN*2]; jsonEsc(errs[i],e2,sizeof(e2));
        if(i) printf(",");
        printf("\"%s\"",e2);
    }
    printf("]}\n");
}

/* ═══════════════════════════════════════════════════════
   MAIN
   stdin:  line 1 = language tag, rest = source code
═══════════════════════════════════════════════════════ */
int main(){
    static char raw[MAX_SOURCE];
    int n=(int)fread(raw,1,MAX_SOURCE-1,stdin);
    raw[n]='\0';

    if(n==0){
        printf("{\"tokens\":[],\"ir\":[],\"python\":\"\",\"c\":\"\",\"cpp\":\"\",\"java\":\"\",\"srclang\":\"?\",\"errors\":[\"Empty input\"]}\n");
        return 0;
    }

    /* extract language tag from first line */
    char *nl=strchr(raw,'\n');
    const char *src=raw;
    if(nl){
        int tl=(int)(nl-raw); if(tl>14)tl=14;
        strncpy(srcLang,raw,tl); srcLang[tl]='\0';
        if(tl>0&&srcLang[tl-1]=='\r') srcLang[tl-1]='\0';
        src=nl+1;
    }

    lexAll(src);

    if     (!strcmp(srcLang,"c"))    parseC();
    else if(!strcmp(srcLang,"cpp"))  parseCpp();
    else if(!strcmp(srcLang,"java")) parseJava();
    else                             parsePython();  /* python / custom */

    outputJSON();
    return 0;
}