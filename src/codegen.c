/*
 * ================================================================
 *  codegen.c  —  CODE GENERATOR IMPLEMENTATIONS
 *
 *  Internal structure
 *  ──────────────────
 *  indentN(buf, depth, sz)
 *    Append `depth * 4` spaces to buf.
 *
 *  genCallExpr(lang, obj, method, args, out, outsz)
 *    Translate a method call to the target-language idiom.
 *    Knows about push_back/add/append, size/len, pop_back, front,
 *    back, clear.  Falls back to a generic call form.
 *
 *  genContainerDecl(lang, node, line, sz)
 *    Translate an IR_CONTAINER_DECL node to the correct declaration
 *    syntax: vector→ArrayList, array→int[], map→HashMap, etc.
 *
 *  genFieldDecl(lang, node, line, sz)
 *    Translate an IR_FIELD_DECL node (inside a class) to the
 *    target-language member-variable declaration.
 *
 *  emitLineC / emitLineCpp / emitLineJava / emitLinePython
 *    Per-language statement emitters.  Take one IRNode and append
 *    the rendered line to `buf`, managing brace depth via `*depth`.
 *
 *  genC / genCpp / genJava / genPython
 *    Top-level generators.  Walk ir[], dispatch class/method/main
 *    framing IR nodes, and call the corresponding emitLine* for
 *    every body statement.
 * ================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"
#include "containers.h"
#include "codegen.h"

/* ═══════════════════════════════════════════════════════════════════
   DECLARATION TRACKING  —  prevents re-emitting the type prefix on
   variables that were already declared earlier in the same scope.

   Problem without this:
     IR_ASSIGN  dest="x"  src1="x"  op="+"  src2="1"   (compound x+=1)
     → "int x = x + 1;"   ← invalid: redeclares x in the same scope

   Solution:
     gDecl tracks every name declared in the current flat scope.
     emitLine* checks before emitting a type prefix; if the name is
     already known it drops the type and emits just "x = ...".

   Scoping:
     declReset() is called at the start of each gen* function and
     again at every IR_METHOD_START / IR_CTOR_START / IR_MAIN_START
     so each method body gets a fresh scope.  The loop variable in
     an IR_FOR node is NOT added (it is scoped to the for-init and
     already declared inline in the emitted statement).
═══════════════════════════════════════════════════════════════════ */
typedef struct {
    char names[MAX_IR_NODES][MAX_NAME_LEN];
    int  count;
} DeclSet;

static DeclSet gDecl;

static void declReset(void) { gDecl.count = 0; }

static int declHas(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < gDecl.count; i++)
        if (!strcmp(gDecl.names[i], name)) return 1;
    return 0;
}

static void declAdd(const char *name) {
    if (!name || !name[0] || gDecl.count >= MAX_IR_NODES) return;
    if (!declHas(name))
        strncpy(gDecl.names[gDecl.count++], name, MAX_NAME_LEN-1);
}

/* ═══════════════════════════════════════════════════════════════════
   INDENTATION
═══════════════════════════════════════════════════════════════════ */
static void indentN(char *buf, int depth, int sz) {
    char s[80]="";
    int sp = depth * 4; if (sp > 76) sp = 76;
    for (int i=0; i<sp; i++) s[i]=' ';
    s[sp]='\0'; app(buf,s,sz);
}

/* ═══════════════════════════════════════════════════════════════════
   METHOD-CALL EXPRESSION  →  PER-LANGUAGE IDIOM
   Translates common container method names to the closest native
   equivalent in each target language.
═══════════════════════════════════════════════════════════════════ */
static void genCallExpr(const char *lang,
                        const char *obj, const char *method, const char *args,
                        char *out, int outsz) {
    ContainerInfo *c = findContainer(obj);

    /* ── push / add / append ── */
    if (!strcmp(method,"push_back")||!strcmp(method,"append")||
        !strcmp(method,"add")     ||!strcmp(method,"push")) {
        if      (!strcmp(lang,"cpp"))    snprintf(out,outsz,"%s.push_back(%s)",obj,args);
        else if (!strcmp(lang,"java"))   snprintf(out,outsz,"%s.add(%s)",obj,args);
        else if (!strcmp(lang,"python")) snprintf(out,outsz,"%s.append(%s)",obj,args);
        else snprintf(out,outsz,"/* push_back unsupported for fixed-size C arrays */ (void)0");
        return;
    }

    /* ── size / length ── */
    if (!strcmp(method,"size")||!strcmp(method,"length")) {
        if (!strcmp(lang,"cpp")||!strcmp(lang,"java"))
            snprintf(out,outsz,"%s.size()",obj);
        else if (!strcmp(lang,"python")) snprintf(out,outsz,"len(%s)",obj);
        else {
            char szOnly[24]="0";
            if (c && c->ctorArgs[0]) {
                strncpy(szOnly,c->ctorArgs,23);
                char *comma=strchr(szOnly,','); if(comma) *comma='\0';
            }
            snprintf(out,outsz,"%s",szOnly);
        }
        return;
    }

    /* ── pop_back / pop ── */
    if (!strcmp(method,"pop_back")||!strcmp(method,"pop")) {
        if      (!strcmp(lang,"cpp"))    snprintf(out,outsz,"%s.pop_back()",obj);
        else if (!strcmp(lang,"java"))   snprintf(out,outsz,"%s.remove(%s.size()-1)",obj,obj);
        else if (!strcmp(lang,"python")) snprintf(out,outsz,"%s.pop()",obj);
        else snprintf(out,outsz,"/* pop_back unsupported in C */ (void)0");
        return;
    }

    /* ── front ── */
    if (!strcmp(method,"front")) {
        if (!strcmp(lang,"java")) snprintf(out,outsz,"%s.get(0)",obj);
        else                      snprintf(out,outsz,"%s[0]",obj);
        return;
    }

    /* ── back ── */
    if (!strcmp(method,"back")) {
        if (!strcmp(lang,"java")) {
            snprintf(out,outsz,"%s.get(%s.size()-1)",obj,obj);
        } else if (!strcmp(lang,"python")) {
            snprintf(out,outsz,"%s[-1]",obj);
        } else {
            char szOnly[24]="1";
            if (c && c->ctorArgs[0]) {
                strncpy(szOnly,c->ctorArgs,23);
                char *comma=strchr(szOnly,','); if(comma) *comma='\0';
            }
            snprintf(out,outsz,"%s[%s-1]",obj,szOnly);
        }
        return;
    }

    /* ── clear ── */
    if (!strcmp(method,"clear")) {
        if (!strcmp(lang,"cpp")||!strcmp(lang,"java")||!strcmp(lang,"python"))
            snprintf(out,outsz,"%s.clear()",obj);
        else
            snprintf(out,outsz,"/* clear unsupported in C */ (void)0");
        return;
    }

    /* ── generic fallback ── */
    if (!strcmp(lang,"java")||!strcmp(lang,"cpp")||!strcmp(lang,"python"))
        snprintf(out,outsz,"%s.%s(%s)",obj,method,args);
    else
        snprintf(out,outsz,"%s_%s(&%s%s%s)",
                 obj,method,obj,(args[0]?",":""),args);
}

/* ═══════════════════════════════════════════════════════════════════
   CONTAINER DECLARATION  →  PER-LANGUAGE STATEMENT
═══════════════════════════════════════════════════════════════════ */
static void genContainerDecl(const char *lang, IRNode *n, char *line, int sz) {
    const char *name = n->dest;
    const char *elem = n->src1;   /* element type / "K:V" for map */
    const char *kind = n->op;     /* vector / list / map / set / array */
    const char *args = n->src2;   /* raw constructor args */

    char num1[20]="", num2[20]="0";
    if (args[0]) {
        strncpy(num1, args, 19);
        char *comma = strchr(num1,',');
        if (comma) { strncpy(num2, comma+1, 19); *comma='\0'; }
    }

    /* ── fixed-size array ── */
    if (!strcmp(kind,"array")) {
        if (!strcmp(lang,"c")||!strcmp(lang,"cpp"))
            snprintf(line,sz,"%s %s[%s];\n",elem,name,num1[0]?num1:"1");
        else if (!strcmp(lang,"java"))
            snprintf(line,sz,"%s[] %s = new %s[%s];\n",elem,name,elem,num1[0]?num1:"1");
        else
            snprintf(line,sz,"%s = [0] * %s\n",name,num1[0]?num1:"1");
        return;
    }

    /* ── map ── */
    if (!strcmp(kind,"map")) {
        char k[20]="int",v[20]="int";
        char tmp[42]; strncpy(tmp,elem,41); tmp[41]='\0';
        char *colon=strchr(tmp,':');
        if (colon) { *colon='\0'; strncpy(k,tmp,19); strncpy(v,colon+1,19); }
        if      (!strcmp(lang,"cpp"))    snprintf(line,sz,"map<%s,%s> %s;\n",k,v,name);
        else if (!strcmp(lang,"java"))   snprintf(line,sz,"HashMap<%s,%s> %s = new HashMap<>();\n",k,v,name);
        else if (!strcmp(lang,"python")) snprintf(line,sz,"%s = {}\n",name);
        else snprintf(line,sz,"/* map<%s,%s> %s — no native map in C */\n",k,v,name);
        return;
    }

    /* ── list ── */
    if (!strcmp(kind,"list")) {
        if      (!strcmp(lang,"cpp"))    snprintf(line,sz,"list<%s> %s;\n",elem,name);
        else if (!strcmp(lang,"java"))   snprintf(line,sz,"LinkedList<%s> %s = new LinkedList<>();\n",elem,name);
        else if (!strcmp(lang,"python")) snprintf(line,sz,"%s = []\n",name);
        else snprintf(line,sz,"%s *%s = NULL; /* list — manual alloc needed */\n",elem,name);
        return;
    }

    /* ── set ── */
    if (!strcmp(kind,"set")) {
        if      (!strcmp(lang,"cpp"))    snprintf(line,sz,"set<%s> %s;\n",elem,name);
        else if (!strcmp(lang,"java"))   snprintf(line,sz,"HashSet<%s> %s = new HashSet<>();\n",elem,name);
        else if (!strcmp(lang,"python")) snprintf(line,sz,"%s = set()\n",name);
        else snprintf(line,sz,"/* set<%s> %s — no native set in C */\n",elem,name);
        return;
    }

    /* ── vector (default) ── */
    if (!strcmp(lang,"cpp")) {
        if (args[0]) snprintf(line,sz,"vector<%s> %s(%s);\n",elem,name,args);
        else         snprintf(line,sz,"vector<%s> %s;\n",elem,name);
    } else if (!strcmp(lang,"java")) {
        const char *boxed =
            !strcmp(elem,"int")    ? "Integer" :
            !strcmp(elem,"float")  ? "Float"   :
            !strcmp(elem,"double") ? "Double"  :
            !strcmp(elem,"char")   ? "Character":
            !strcmp(elem,"bool")   ? "Boolean"  : "String";
        if (num1[0] && args[0] && strchr(args,','))
            snprintf(line,sz,"ArrayList<%s> %s = new ArrayList<>(Collections.nCopies(%s, %s));\n",
                     boxed,name,num1,num2);
        else
            snprintf(line,sz,"ArrayList<%s> %s = new ArrayList<>();\n",boxed,name);
    } else if (!strcmp(lang,"python")) {
        if (num1[0] && args[0] && strchr(args,','))
            snprintf(line,sz,"%s = [%s] * %s\n",name,num2,num1);
        else
            snprintf(line,sz,"%s = []\n",name);
    } else { /* C */
        if (num1[0])
            snprintf(line,sz,"%s *%s = calloc(%s, sizeof(%s)); /* vector(%s) */\n",
                     elem,name,num1,elem,args);
        else
            snprintf(line,sz,"%s *%s = NULL; /* dynamic vector */\n",elem,name);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   FIELD DECLARATION  →  PER-LANGUAGE CLASS MEMBER
═══════════════════════════════════════════════════════════════════ */
static void genFieldDecl(const char *lang, IRNode *n, char *line, int sz) {
    const char *name    = n->dest;
    const char *initVal = n->src1;
    const char *mods    = n->op;
    const char *type    = n->dtype[0] ? n->dtype : "int";

    int isStatic = strstr(mods,"static")   != NULL;
    int isConst  = strstr(mods,"const")    != NULL;
    const char *vis =
        strstr(mods,"private")   ? "private"   :
        strstr(mods,"protected") ? "protected" : "public";

    if (!strcmp(lang,"cpp")) {
        char prefix[24]="";
        if (isStatic) strcat(prefix,"static ");
        if (isConst)  strcat(prefix,"const ");
        if (initVal[0]) snprintf(line,sz,"%s%s %s = %s;\n",prefix,type,name,initVal);
        else            snprintf(line,sz,"%s%s %s;\n",prefix,type,name);

    } else if (!strcmp(lang,"c")) {
        /* C structs cannot have inline initializers; value applied in _create() */
        snprintf(line,sz,"%s %s;\n",type,name);

    } else if (!strcmp(lang,"java")) {
        char prefix[40]; snprintf(prefix,sizeof(prefix),"%s ",vis);
        if (isStatic) strcat(prefix,"static ");
        if (isConst)  strcat(prefix,"final ");
        if (initVal[0]) snprintf(line,sz,"%s%s %s = %s;\n",prefix,type,name,initVal);
        else            snprintf(line,sz,"%s%s %s;\n",prefix,type,name);

    } else { /* python — caller normally handles class fields directly */
        if (initVal[0]) snprintf(line,sz,"self.%s = %s\n",name,initVal);
        else            snprintf(line,sz,"self.%s = 0\n",name);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   PER-LANGUAGE STATEMENT EMITTERS
   Each function appends one rendered IR statement to `buf`.
   `depth` is read/written to track brace nesting.
═══════════════════════════════════════════════════════════════════ */

/* ── C statement emitter ── */
static void emitLineC(IRNode *n, char *line, int sz, int *depth,
                      char *buf, int bufsz) {
    char tp[20];
    if (n->type==IR_BLOCK_START) { (*depth)++; return; }
    if (n->type==IR_BLOCK_END) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth,bufsz); app(buf,"}\n",bufsz); return;
    }
    if (n->type==IR_ELSE) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth-1,bufsz); app(buf,"} else {\n",bufsz); (*depth)++; return;
    }
    indentN(buf,*depth,bufsz);
    strncpy(tp, n->dtype[0]?n->dtype:"int", 19); tp[19]='\0';
    if (!strcmp(tp,"auto"))   strcpy(tp,"int");
    if (!strcmp(tp,"String")) strcpy(tp,"char*");

    if (n->type==IR_ASSIGN) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s;\n",n->dest,n->src1);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s;\n",tp,n->dest,n->src1); }
    }
    else if (n->type==IR_BINOP) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s %s %s;\n",n->dest,n->src1,n->op,n->src2);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2); }
    }
    else if (n->type==IR_PRINT)  snprintf(line,sz,"printf(\"%%d\\n\", %s);\n",n->dest);
    else if (n->type==IR_IF)   { snprintf(line,sz,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_WHILE){ snprintf(line,sz,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_FOR)  { snprintf(line,sz,"for (int %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_RETURN) snprintf(line,sz,"return %s;\n",n->src1[0]?n->src1:"0");
    else if (n->type==IR_CONTAINER_DECL) { genContainerDecl("c",n,line,sz); declAdd(n->dest); }
    else if (n->type==IR_CALL) {
        char expr[256]; genCallExpr("c",n->dest,n->src1,n->src2,expr,sizeof(expr));
        if (n->dtype[0]) {
            if (declHas(n->dtype)) snprintf(line,sz,"%s = %s;\n",n->dtype,expr);
            else { declAdd(n->dtype); snprintf(line,sz,"int %s = %s;\n",n->dtype,expr); }
        } else snprintf(line,sz,"%s;\n",expr);
    }
    else if (n->type==IR_FIELD_ACCESS) {
        if (n->dest[0]) {
            if (declHas(n->dest)) snprintf(line,sz,"%s = %s->%s;\n",n->dest,n->src1,n->src2);
            else { declAdd(n->dest); snprintf(line,sz,"int %s = %s->%s;\n",n->dest,n->src1,n->src2); }
        } else snprintf(line,sz,"%s->%s;\n",n->src1,n->src2);
    }
    else if (n->type==IR_FIELD_SET) snprintf(line,sz,"%s->%s = %s;\n",n->dest,n->src1,n->src2);
    else if (n->type==IR_NEW) {
        declAdd(n->dest);
        snprintf(line,sz,"%s %s = %s_create(%s);\n",n->src1,n->dest,n->src1,n->src2);
    }
    else { line[0]='\0'; return; }
    app(buf,line,bufsz);
}

/* ── C++ statement emitter ── */
static void emitLineCpp(IRNode *n, char *line, int sz, int *depth,
                        char *buf, int bufsz) {
    if (n->type==IR_BLOCK_START) { (*depth)++; return; }
    if (n->type==IR_BLOCK_END) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth,bufsz); app(buf,"}\n",bufsz); return;
    }
    if (n->type==IR_ELSE) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth-1,bufsz); app(buf,"} else {\n",bufsz); (*depth)++; return;
    }
    indentN(buf,*depth,bufsz);
    char tp[20]; strncpy(tp,n->dtype[0]?n->dtype:"auto",19); tp[19]='\0';

    if (n->type==IR_ASSIGN) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s;\n",n->dest,n->src1);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s;\n",tp,n->dest,n->src1); }
    }
    else if (n->type==IR_BINOP) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s %s %s;\n",n->dest,n->src1,n->op,n->src2);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2); }
    }
    else if (n->type==IR_PRINT)  snprintf(line,sz,"cout << %s << endl;\n",n->dest);
    else if (n->type==IR_IF)   { snprintf(line,sz,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_WHILE){ snprintf(line,sz,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_FOR)  { snprintf(line,sz,"for (auto %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_RETURN) snprintf(line,sz,"return %s;\n",n->src1[0]?n->src1:"0");
    else if (n->type==IR_CONTAINER_DECL) { genContainerDecl("cpp",n,line,sz); declAdd(n->dest); }
    else if (n->type==IR_CALL) {
        char expr[256]; genCallExpr("cpp",n->dest,n->src1,n->src2,expr,sizeof(expr));
        if (n->dtype[0]) {
            if (declHas(n->dtype)) snprintf(line,sz,"%s = %s;\n",n->dtype,expr);
            else { declAdd(n->dtype); snprintf(line,sz,"auto %s = %s;\n",n->dtype,expr); }
        } else snprintf(line,sz,"%s;\n",expr);
    }
    else if (n->type==IR_FIELD_ACCESS) {
        const char *op = strcmp(n->op,"->")==0 ? "->" : ".";
        if (n->dest[0]) {
            if (declHas(n->dest)) snprintf(line,sz,"%s = %s%s%s;\n",n->dest,n->src1,op,n->src2);
            else { declAdd(n->dest); snprintf(line,sz,"auto %s = %s%s%s;\n",n->dest,n->src1,op,n->src2); }
        } else snprintf(line,sz,"%s%s%s;\n",n->src1,op,n->src2);
    }
    else if (n->type==IR_FIELD_SET) {
        const char *op = strcmp(n->op,"->")==0 ? "->" : ".";
        snprintf(line,sz,"%s%s%s = %s;\n",n->dest,op,n->src1,n->src2);
    }
    else if (n->type==IR_NEW) {
        declAdd(n->dest);
        snprintf(line,sz,"%s %s(%s);\n",n->src1,n->dest,n->src2);
    }
    else { line[0]='\0'; return; }
    app(buf,line,bufsz);
}

/* ── Java statement emitter ── */
static void emitLineJava(IRNode *n, char *line, int sz, int *depth,
                         char *buf, int bufsz) {
    if (n->type==IR_BLOCK_START) { (*depth)++; return; }
    if (n->type==IR_BLOCK_END) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth,bufsz); app(buf,"}\n",bufsz); return;
    }
    if (n->type==IR_ELSE) {
        if (*depth>1) (*depth)--;
        indentN(buf,*depth-1,bufsz); app(buf,"} else {\n",bufsz); (*depth)++; return;
    }
    indentN(buf,*depth,bufsz);
    char tp[20]; strncpy(tp,n->dtype[0]?n->dtype:"int",19); tp[19]='\0';
    if (!strcmp(tp,"auto")) strcpy(tp,"int");

    if (n->type==IR_ASSIGN) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s;\n",n->dest,n->src1);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s;\n",tp,n->dest,n->src1); }
    }
    else if (n->type==IR_BINOP) {
        if (declHas(n->dest)) snprintf(line,sz,"%s = %s %s %s;\n",n->dest,n->src1,n->op,n->src2);
        else { declAdd(n->dest); snprintf(line,sz,"%s %s = %s %s %s;\n",tp,n->dest,n->src1,n->op,n->src2); }
    }
    else if (n->type==IR_PRINT)  snprintf(line,sz,"System.out.println(%s);\n",n->dest);
    else if (n->type==IR_IF)   { snprintf(line,sz,"if (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_WHILE){ snprintf(line,sz,"while (%s %s %s) {\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_FOR)  { snprintf(line,sz,"for (int %s = %s; %s %s %s; %s++) {\n",n->dest,n->src1,n->dest,n->op,n->src2,n->dest); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_RETURN) {
        if (!n->src1[0] || !strcmp(n->src1,"0")) return;
        snprintf(line,sz,"return %s;\n",n->src1);
    }
    else if (n->type==IR_CONTAINER_DECL) { genContainerDecl("java",n,line,sz); declAdd(n->dest); }
    else if (n->type==IR_CALL) {
        char expr[256]; genCallExpr("java",n->dest,n->src1,n->src2,expr,sizeof(expr));
        if (n->dtype[0]) {
            if (declHas(n->dtype)) snprintf(line,sz,"%s = %s;\n",n->dtype,expr);
            else { declAdd(n->dtype); snprintf(line,sz,"int %s = %s;\n",n->dtype,expr); }
        } else snprintf(line,sz,"%s;\n",expr);
    }
    else if (n->type==IR_FIELD_ACCESS) {
        if (n->dest[0]) {
            if (declHas(n->dest)) snprintf(line,sz,"%s = %s.%s;\n",n->dest,n->src1,n->src2);
            else { declAdd(n->dest); snprintf(line,sz,"int %s = %s.%s;\n",n->dest,n->src1,n->src2); }
        } else snprintf(line,sz,"%s.%s;\n",n->src1,n->src2);
    }
    else if (n->type==IR_FIELD_SET) snprintf(line,sz,"%s.%s = %s;\n",n->dest,n->src1,n->src2);
    else if (n->type==IR_NEW) {
        declAdd(n->dest);
        snprintf(line,sz,"%s %s = new %s(%s);\n",n->src1,n->dest,n->src1,n->src2);
    }
    else { line[0]='\0'; return; }
    app(buf,line,bufsz);
}

/* ── Python statement emitter ── */
static void emitLinePython(IRNode *n, char *line, int sz, int *depth,
                           char *buf, int bufsz) {
    if (n->type==IR_BLOCK_START) { return; }
    if (n->type==IR_BLOCK_END)   { if (*depth>0) (*depth)--; return; }
    if (n->type==IR_ELSE) {
        if (*depth>0) (*depth)--;
        indentN(buf,*depth,bufsz); app(buf,"else:\n",bufsz); (*depth)++; return;
    }
    indentN(buf,*depth,bufsz);

    if      (n->type==IR_ASSIGN) snprintf(line,sz,"%s = %s\n",n->dest,n->src1);
    else if (n->type==IR_BINOP)  snprintf(line,sz,"%s = %s %s %s\n",n->dest,n->src1,n->op,n->src2);
    else if (n->type==IR_PRINT)  snprintf(line,sz,"print(%s)\n",n->dest);
    else if (n->type==IR_RETURN) {
        if (*depth > 0) snprintf(line,sz,"return %s\n",n->src1[0]?n->src1:"");
        else { line[0]='\0'; return; }
    }
    else if (n->type==IR_IF)   { snprintf(line,sz,"if %s %s %s:\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_WHILE){ snprintf(line,sz,"while %s %s %s:\n",n->src1,n->op,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_FOR)  { snprintf(line,sz,"for %s in range(%s, %s):\n",n->dest,n->src1,n->src2); app(buf,line,bufsz); (*depth)++; return; }
    else if (n->type==IR_CONTAINER_DECL) genContainerDecl("python",n,line,sz);
    else if (n->type==IR_CALL) {
        char expr[256]; genCallExpr("python",n->dest,n->src1,n->src2,expr,sizeof(expr));
        if (n->dtype[0]) snprintf(line,sz,"%s = %s\n",n->dtype,expr);
        else             snprintf(line,sz,"%s\n",expr);
    }
    else if (n->type==IR_FIELD_ACCESS) {
        if (n->dest[0]) snprintf(line,sz,"%s = %s.%s\n",n->dest,n->src1,n->src2);
        else            snprintf(line,sz,"%s.%s\n",n->src1,n->src2);
    }
    else if (n->type==IR_FIELD_SET) snprintf(line,sz,"%s.%s = %s\n",n->dest,n->src1,n->src2);
    else if (n->type==IR_NEW)       snprintf(line,sz,"%s = %s(%s)\n",n->dest,n->src1,n->src2);
    else { line[0]='\0'; return; }
    app(buf,line,bufsz);
}

/* ═══════════════════════════════════════════════════════════════════
   TOP-LEVEL GENERATORS
═══════════════════════════════════════════════════════════════════ */

/* ── C ── */
void genC(char *buf, int sz) {
    static char structBuf[MAX_OUTPUT], funcBuf[MAX_OUTPUT], mainB[MAX_OUTPUT];
    buf[0]='\0'; structBuf[0]='\0'; funcBuf[0]='\0'; mainB[0]='\0';
    declReset();

    int inClass=0, inMain=0;
    char className[MAX_NAME_LEN]="";
    int clsDepth=0, mainDepth=1;
    char line[512];

    /* Field initial values — applied in _create() since C structs
       cannot have inline member initializers. */
    char fieldNames[32][MAX_NAME_LEN];
    char fieldInits[32][MAX_NAME_LEN];
    int  fieldN = 0;

    for (int i=0; i<irCount; i++) {
        IRNode *n = &ir[i];

        if (n->type==IR_CLASS_START) {
            inClass=1; fieldN=0;
            strncpy(className, n->dest, MAX_NAME_LEN-1);
            app(structBuf,"typedef struct {\n",sizeof(structBuf));
            continue;
        }
        if (n->type==IR_CLASS_END) {
            char tmp[160]; snprintf(tmp,sizeof(tmp),"} %s;\n\n",className);
            app(structBuf,tmp,sizeof(structBuf)); inClass=0; continue;
        }
        if (n->type==IR_MAIN_START) { inMain=1; declReset(); continue; }
        if (n->type==IR_MAIN_END)   { inMain=0; continue; }

        if (inClass && !inMain) {
            if (n->type==IR_FIELD_DECL) {
                char fl[256]; genFieldDecl("c",n,fl,sizeof(fl));
                app(structBuf,"    ",sizeof(structBuf));
                app(structBuf,fl,sizeof(structBuf));
                if (n->src1[0] && fieldN<32) {
                    strncpy(fieldNames[fieldN], n->dest, MAX_NAME_LEN-1);
                    strncpy(fieldInits[fieldN], n->src1, MAX_NAME_LEN-1);
                    fieldN++;
                }
                continue;
            }
            if (n->type==IR_CTOR_START) {
                char hdr[200];
                snprintf(hdr,sizeof(hdr),
                    "%s %s_create(%s) {\n    %s self = {0};\n",
                    className,className,n->src2,className);
                app(funcBuf,hdr,sizeof(funcBuf));
                declReset();
                for (int f=0;f<fieldN;f++) {
                    char fl[160];
                    snprintf(fl,sizeof(fl),"    self.%s = %s;\n",
                             fieldNames[f],fieldInits[f]);
                    app(funcBuf,fl,sizeof(funcBuf));
                }
                clsDepth=1; continue;
            }
            if (n->type==IR_CTOR_END) {
                app(funcBuf,"    return self;\n}\n\n",sizeof(funcBuf));
                clsDepth=0; continue;
            }
            if (n->type==IR_METHOD_START) {
                char hdr[200];
                snprintf(hdr,sizeof(hdr),"%s %s_%s(%s *self%s%s) {\n",
                    n->src1[0]?n->src1:"void",
                    className, n->dest, className,
                    n->src2[0]?", ":"", n->src2);
                app(funcBuf,hdr,sizeof(funcBuf)); clsDepth=1; continue;
            }
            if (n->type==IR_METHOD_END) {
                app(funcBuf,"}\n\n",sizeof(funcBuf)); clsDepth=0; continue;
            }
            emitLineC(n,line,sizeof(line),&clsDepth,funcBuf,sizeof(funcBuf));
            continue;
        }
        emitLineC(n,line,sizeof(line),&mainDepth,mainB,sizeof(mainB));
    }

    app(buf,"/* Generated by Mini-Transpiler */\n"
            "#include <stdio.h>\n#include <stdlib.h>\n\n",sz);
    app(buf,structBuf,sz);
    app(buf,funcBuf,sz);
    app(buf,"int main() {\n",sz);
    app(buf,mainB,sz);
    if (!irHasReturn()) app(buf,"    return 0;\n}\n",sz);
    else                app(buf,"}\n",sz);
}

/* ── C++ ── */
void genCpp(char *buf, int sz) {
    static char cls[MAX_OUTPUT], mainB[MAX_OUTPUT];
    buf[0]='\0'; cls[0]='\0'; mainB[0]='\0';
    declReset();

    int inClass=0, inMain=0;
    int clsDepth=0, mainDepth=1;
    char line[512], lastVis[16]="";

    for (int i=0; i<irCount; i++) {
        IRNode *n = &ir[i];

        if (n->type==IR_CLASS_START) {
            inClass=1; lastVis[0]='\0';
            char tmp[160]; snprintf(tmp,sizeof(tmp),"class %s {\n",n->dest);
            app(cls,tmp,sizeof(cls)); continue;
        }
        if (n->type==IR_CLASS_END) { app(cls,"};\n\n",sizeof(cls)); inClass=0; continue; }
        if (n->type==IR_MAIN_START) { inMain=1; declReset(); continue; }
        if (n->type==IR_MAIN_END)   { inMain=0; continue; }

        if (inClass && !inMain) {
            if (n->type==IR_FIELD_DECL) {
                const char *vis =
                    strstr(n->op,"private")   ? "private" :
                    strstr(n->op,"protected") ? "protected" : "public";
                if (strcmp(vis,lastVis)) {
                    char hdr[24]; snprintf(hdr,sizeof(hdr),"%s:\n",vis);
                    app(cls,hdr,sizeof(cls)); strncpy(lastVis,vis,15);
                }
                char fl[256]; genFieldDecl("cpp",n,fl,sizeof(fl));
                app(cls,"    ",sizeof(cls)); app(cls,fl,sizeof(cls));
                continue;
            }
            if (n->type==IR_CTOR_START) {
                const char *vis =
                    strstr(n->op,"private")   ? "private" :
                    strstr(n->op,"protected") ? "protected" : "public";
                if (strcmp(vis,lastVis)) {
                    char hdr[24]; snprintf(hdr,sizeof(hdr),"%s:\n",vis);
                    app(cls,hdr,sizeof(cls)); strncpy(lastVis,vis,15);
                }
                char hdr[160]; snprintf(hdr,sizeof(hdr),"    %s(%s) {\n",n->dest,n->src2);
                app(cls,hdr,sizeof(cls)); declReset(); clsDepth=2; continue;
            }
            if (n->type==IR_CTOR_END) { app(cls,"    }\n\n",sizeof(cls)); clsDepth=0; continue; }
            if (n->type==IR_METHOD_START) {
                const char *vis =
                    strstr(n->op,"private")   ? "private" :
                    strstr(n->op,"protected") ? "protected" : "public";
                if (strcmp(vis,lastVis)) {
                    char hdr[24]; snprintf(hdr,sizeof(hdr),"%s:\n",vis);
                    app(cls,hdr,sizeof(cls)); strncpy(lastVis,vis,15);
                }
                char hdr[200];
                snprintf(hdr,sizeof(hdr),"    %s %s(%s) {\n",
                    n->src1[0]?n->src1:"void", n->dest, n->src2);
                app(cls,hdr,sizeof(cls)); declReset(); clsDepth=2; continue;
            }
            if (n->type==IR_METHOD_END) { app(cls,"    }\n\n",sizeof(cls)); clsDepth=0; continue; }
            emitLineCpp(n,line,sizeof(line),&clsDepth,cls,sizeof(cls));
            continue;
        }
        emitLineCpp(n,line,sizeof(line),&mainDepth,mainB,sizeof(mainB));
    }

    app(buf,"// Generated by Mini-Transpiler\n"
            "#include <iostream>\n#include <vector>\n"
            "#include <list>\n#include <map>\n#include <set>\n"
            "using namespace std;\n\n",sz);
    app(buf,cls,sz);
    app(buf,"int main() {\n",sz);
    app(buf,mainB,sz);
    if (!irHasReturn()) app(buf,"    return 0;\n}\n",sz);
    else                app(buf,"}\n",sz);
}

/* ── Java ── */
void genJava(char *buf, int sz) {
    static char cls[MAX_OUTPUT], mainB[MAX_OUTPUT];
    buf[0]='\0'; cls[0]='\0'; mainB[0]='\0';
    declReset();

    int sawClass=0, inMain=0;
    char className[MAX_NAME_LEN]="Output";
    int clsDepth=1, mainDepth=2;
    char line[512];

    for (int i=0; i<irCount; i++) {
        IRNode *n = &ir[i];

        if (n->type==IR_CLASS_START) {
            sawClass=1; strncpy(className,n->dest,MAX_NAME_LEN-1);
            char tmp[160]; snprintf(tmp,sizeof(tmp),"public class %s {\n",className);
            app(cls,tmp,sizeof(cls)); continue;
        }
        if (n->type==IR_CLASS_END) { app(cls,"}\n",sizeof(cls)); continue; }
        if (n->type==IR_MAIN_START) {
            inMain=1;
            app(cls,"    public static void main(String[] args) {\n",sizeof(cls));
            mainB[0]='\0'; mainDepth=2; declReset(); continue;
        }
        if (n->type==IR_MAIN_END) {
            inMain=0;
            app(cls,mainB,sizeof(cls));
            app(cls,"    }\n",sizeof(cls));
            mainB[0]='\0'; continue;
        }

        if (inMain) {
            emitLineJava(n,line,sizeof(line),&mainDepth,mainB,sizeof(mainB));
            continue;
        }
        if (sawClass) {
            if (n->type==IR_FIELD_DECL) {
                char fl[256]; genFieldDecl("java",n,fl,sizeof(fl));
                app(cls,"    ",sizeof(cls)); app(cls,fl,sizeof(cls)); continue;
            }
            if (n->type==IR_CTOR_START) {
                const char *vis =
                    strstr(n->op,"private")   ? "private" :
                    strstr(n->op,"protected") ? "protected" : "public";
                char hdr[160]; snprintf(hdr,sizeof(hdr),"    %s %s(%s) {\n",vis,n->dest,n->src2);
                app(cls,hdr,sizeof(cls)); declReset(); clsDepth=2; continue;
            }
            if (n->type==IR_CTOR_END) { app(cls,"    }\n",sizeof(cls)); clsDepth=1; continue; }
            if (n->type==IR_METHOD_START) {
                const char *vis =
                    strstr(n->op,"private")   ? "private" :
                    strstr(n->op,"protected") ? "protected" : "public";
                int isStatic = strstr(n->op,"static") != NULL;
                char hdr[200];
                snprintf(hdr,sizeof(hdr),"    %s %s%s %s(%s) {\n",
                    vis, isStatic?"static ":"",
                    n->src1[0]?n->src1:"void", n->dest, n->src2);
                app(cls,hdr,sizeof(cls)); declReset(); clsDepth=2; continue;
            }
            if (n->type==IR_METHOD_END) { app(cls,"    }\n",sizeof(cls)); clsDepth=1; continue; }
            emitLineJava(n,line,sizeof(line),&clsDepth,cls,sizeof(cls));
            continue;
        }
        /* no class in source — flat statements go into mainB */
        emitLineJava(n,line,sizeof(line),&mainDepth,mainB,sizeof(mainB));
    }

    app(buf,"// Generated by Mini-Transpiler\n",sz);
    if (sawClass) {
        app(buf,cls,sz);
    } else {
        char hdr[160];
        snprintf(hdr,sizeof(hdr),
            "public class %s {\n"
            "    public static void main(String[] args) {\n", className);
        app(buf,hdr,sz);
        app(buf,mainB,sz);
        app(buf,"    }\n}\n",sz);
    }
}

/* ── Python ── */
void genPython(char *buf, int sz) {
    static char cls[MAX_OUTPUT], mainB[MAX_OUTPUT];
    buf[0]='\0'; cls[0]='\0'; mainB[0]='\0';

    int inClass=0, inMain=0, inCtor=0, inMethod=0;
    int clsDepth=1, mainDepth=0;
    char line[512];

    for (int i=0; i<irCount; i++) {
        IRNode *n = &ir[i];

        if (n->type==IR_CLASS_START) {
            inClass=1;
            char tmp[160]; snprintf(tmp,sizeof(tmp),"class %s:\n",n->dest);
            app(cls,tmp,sizeof(cls)); continue;
        }
        if (n->type==IR_CLASS_END) { app(cls,"\n",sizeof(cls)); inClass=0; continue; }
        if (n->type==IR_MAIN_START) { inMain=1; continue; }
        if (n->type==IR_MAIN_END)   { inMain=0; mainDepth=0; continue; }

        if (inClass && !inMain) {
            if (n->type==IR_FIELD_DECL) {
                const char *vis = strstr(n->op,"private") ? "_" : "";
                char tmp[200];
                snprintf(tmp,sizeof(tmp),"    %s%s = %s\n",
                    vis, n->dest, n->src1[0]?n->src1:"0");
                app(cls,tmp,sizeof(cls)); continue;
            }
            if (n->type==IR_CTOR_START) {
                char hdr[160];
                snprintf(hdr,sizeof(hdr),"    def __init__(self%s%s):\n",
                    n->src2[0]?", ":"", n->src2);
                app(cls,hdr,sizeof(cls)); clsDepth=2; inCtor=1; continue;
            }
            if (n->type==IR_CTOR_END) { inCtor=0; clsDepth=1; continue; }
            if (n->type==IR_METHOD_START) {
                char hdr[200];
                snprintf(hdr,sizeof(hdr),"    def %s(self%s%s):\n",
                    n->dest, n->src2[0]?", ":"", n->src2);
                app(cls,hdr,sizeof(cls)); clsDepth=2; inMethod=1; continue;
            }
            if (n->type==IR_METHOD_END) { inMethod=0; clsDepth=1; continue; }

            if (inCtor || inMethod) {
                /* Rewrite bare assignments as self.<name> = … */
                if (n->type==IR_ASSIGN || n->type==IR_BINOP) {
                    indentN(cls,clsDepth,sizeof(cls));
                    if (n->type==IR_ASSIGN)
                        snprintf(line,sizeof(line),"self.%s = %s\n",n->dest,n->src1);
                    else
                        snprintf(line,sizeof(line),"self.%s = %s %s %s\n",
                            n->dest,n->src1,n->op,n->src2);
                    app(cls,line,sizeof(cls)); continue;
                }
                emitLinePython(n,line,sizeof(line),&clsDepth,cls,sizeof(cls));
            }
            continue;
        }
        emitLinePython(n,line,sizeof(line),&mainDepth,mainB,sizeof(mainB));
    }

    app(buf,"# Generated by Mini-Transpiler\n\n",sz);
    app(buf,cls,sz);
    app(buf,mainB,sz);
}
