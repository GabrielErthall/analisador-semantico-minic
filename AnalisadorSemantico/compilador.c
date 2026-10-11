/* Analisador semântico da linguagem MINIC — pipeline completo em C (v2).
   Espelha o semantic.py (v6.2): mesmas regras, mesmas mensagens canônicas
   (SEM001–SEM013), mesmos códigos de saída.

   v2: correção do bug de tokens — a versão anterior guardava o tipo do
   token como ponteiro para buffers temporários da função de varredura,
   que eram reescritos a cada iteração (comportamento indefinido); o
   parser então comparava contra conteúdo inválido (ex.: “esperado };”
   no caso 01). Agora o tipo é um array próprio do token, copiado na
   hora. Buffers de mensagem ampliados para eliminar os avisos de
   truncamento do gcc -Wall.

   Compilação: gcc -Wall -Wextra compilador.c -o minic
   Uso:        ./minic <arquivo.c>
   Exit codes: 0 aceito | 2 erro léxico | 3 erro sintático | 4 erro semântico
*/
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXTOKS   8192
#define MAXLEX    256
#define MAXKIDS   96
#define MAXSCOPES 128
#define MAXDECLS  32
#define MAXPARAMS 16
#define MAXFUNCS  64
#define MAXERRS   64
#define MSGLEN    768
#define SYNMSGLEN 512

/* ============================== 1. SCANNER ============================== */
static const char *KEYWORDS[] = {"int","float","bool","void","if","else","while",
    "for","return","break","continue","print","read","true","false",NULL};

typedef struct { char kind[MAXLEX]; char lexeme[MAXLEX]; int line, col; } CTok;

static CTok toks[MAXTOKS];
static int ntoks, tp;

static int is_kw(const char *w) {
    for (int i = 0; KEYWORDS[i]; i++)
        if (!strcmp(w, KEYWORDS[i])) return 1;
    return 0;
}

static void addtok(const char *kind, const char *lex, int l, int c) {
    if (ntoks >= MAXTOKS) return;
    CTok *t = &toks[ntoks++];
    snprintf(t->kind, MAXLEX, "%s", kind);
    snprintf(t->lexeme, MAXLEX, "%s", lex);
    t->line = l; t->col = c;
}

static char lexmsg[SYNMSGLEN];
static int lexline, lexcol, lex_fail;

static int two_char_op(const char *s) {
    char two[3] = {s[0], s[1], 0};
    return !strcmp(two,"==") || !strcmp(two,"!=") || !strcmp(two,"<=") ||
           !strcmp(two,">=") || !strcmp(two,"&&") || !strcmp(two,"||");
}

static int scan_src(const char *s) {
    int i = 0, line = 1, col = 1, n = (int)strlen(s);
    ntoks = 0; lex_fail = 0;
    while (i < n) {
        char ch = s[i];
        if (ch==' '||ch=='\t'||ch=='\r'||ch=='\n') {
            if (ch=='\n'){ line++; col=1; } else col++;
            i++; continue;
        }
        if (ch=='/' && s[i+1]=='/') { while (i<n && s[i]!='\n'){ i++; col++; } continue; }
        if (ch=='/' && s[i+1]=='*') {
            int sl=line, sc=col; i+=2; col+=2;
            while (i<n && !(s[i]=='*' && s[i+1]=='/')) {
                if (s[i]=='\n'){ line++; col=1; } else col++;
                i++;
            }
            if (i>=n) { snprintf(lexmsg,SYNMSGLEN,"comentário de bloco não terminado");
                        lexline=sl; lexcol=sc; lex_fail=1; return 0; }
            i+=2; col+=2; continue;
        }
        int tl=line, tc=col;
        char buf[MAXLEX];
        if (isalpha((unsigned char)ch) || ch=='_') {
            int j=0;
            while (i<n && (isalnum((unsigned char)s[i]) || s[i]=='_')) { buf[j++]=s[i++]; col++; }
            buf[j]=0;
            addtok(is_kw(buf)?buf:"id", buf, tl, tc);
            continue;
        }
        if (isdigit((unsigned char)ch)) {
            int j=0;
            while (i<n && isdigit((unsigned char)s[i])) { buf[j++]=s[i++]; col++; }
            buf[j]=0;
            if (i<n && s[i]=='.' && i+1<n && isdigit((unsigned char)s[i+1])) {
                buf[j++]=s[i++]; col++;
                while (i<n && isdigit((unsigned char)s[i])) { buf[j++]=s[i++]; col++; }
                buf[j]=0;
                addtok("real_lit", buf, tl, tc);
            } else addtok("int_lit", buf, tl, tc);
            continue;
        }
        if (ch=='\'') {
            i++; col++;
            int j=0; buf[j++]='\'';
            if (i<n && s[i]=='\\') { buf[j++]=s[i++]; col++; if (i<n){ buf[j++]=s[i++]; col++; } }
            else if (i<n) { buf[j++]=s[i++]; col++; }
            else { snprintf(lexmsg,SYNMSGLEN,"caractere não terminado"); lexline=tl; lexcol=tc; lex_fail=1; return 0; }
            if (i>=n || s[i]!='\'') { snprintf(lexmsg,SYNMSGLEN,"caractere com tamanho inválido"); lexline=tl; lexcol=tc; lex_fail=1; return 0; }
            buf[j++]='\''; i++; col++; buf[j]=0;
            addtok("char_lit", buf, tl, tc);
            continue;
        }
        if (ch=='"') {
            i++; col++;
            int j=0; buf[j++]='"';
            while (i<n && s[i]!='"') {
                if (s[i]=='\\' && i+1<n) { buf[j++]=s[i++]; col++; buf[j++]=s[i++]; col++; }
                else { if (s[i]=='\n'){ line++; col=1; } else col++; buf[j++]=s[i++]; }
            }
            if (i>=n || s[i]!='"') { snprintf(lexmsg,SYNMSGLEN,"cadeia não terminada"); lexline=tl; lexcol=tc; lex_fail=1; return 0; }
            buf[j++]='"'; i++; col++; buf[j]=0;
            addtok("string", buf, tl, tc);
            continue;
        }
        if (i+1<n && two_char_op(s+i)) {
            char two[3]={s[i],s[i+1],0};
            addtok(two, two, tl, tc); i+=2; col+=2; continue;
        }
        if (ch && strchr("+-*/%<>=!()[]{};,", ch)) {
            char one[2]={ch,0};
            addtok(one, one, tl, tc); i++; col++; continue;
        }
        snprintf(lexmsg,SYNMSGLEN,"símbolo \"%c\" não reconhecido",ch);
        lexline=tl; lexcol=tc; lex_fail=1; return 0;
    }
    addtok("eof","",line,col);
    return 1;
}

/* ============================== 2. AST + PARSER ============================== */
typedef struct Node Node;
typedef struct { char type[16]; char name[MAXLEX]; int is_array; int line, col; } Param;
typedef struct { char type[16]; char name[MAXLEX]; Node *init, *size; int is_array; int line, col; } Decl;

enum { N_LIT, N_VAR, N_BIN, N_UN, N_INDEX, N_CALL, N_ASSIGN, N_BLOCK, N_DECL,
       N_IF, N_WHILE, N_FOR, N_RETURN, N_PRINT, N_READ, N_BREAK, N_CONTINUE,
       N_EXPRSTMT, N_EMPTY, N_FUNC, N_PROGRAM };

struct Node {
    int kind;
    int line, col;          /* início da construção */
    int op_line, op_col;    /* N_BIN: posição do operador */
    char op[4];
    char type[16];          /* N_LIT: tipo do literal; N_FUNC: tipo de retorno */
    char lexeme[MAXLEX];    /* N_LIT: lexema; N_VAR: nome; N_FUNC: nome */
    Node *a, *b, *c, *body;
    Node *kids[MAXKIDS]; int nkids;   /* bloco: itens; chamada: argumentos */
    Decl decls[MAXDECLS]; int ndecls;
    Param params[MAXPARAMS]; int nparams;
};

static Node *newnode(int kind) {
    Node *nd = (Node *)calloc(1, sizeof(Node));
    nd->kind = kind;
    return nd;
}

static char synmsg[SYNMSGLEN];
static int syn_fail;

static CTok *pk(void) { return &toks[tp]; }
static CTok *nx(void) { return &toks[tp++]; }

static CTok *expect(const char *kind) {
    CTok *t = pk();
    if (strcmp(t->kind, kind) != 0) {
        snprintf(synmsg, SYNMSGLEN, "esperado %s; encontrado %s (linha %d, coluna %d)",
                 kind, t->lexeme[0] ? t->lexeme : "fim de arquivo", t->line, t->col);
        syn_fail = 1;
        return NULL;
    }
    return nx();
}

static CTok *accept(const char *kind) {
    if (!strcmp(pk()->kind, kind)) return nx();
    return NULL;
}

static const char *parse_type(void) {
    CTok *t = pk();
    if (!strcmp(t->kind,"int")||!strcmp(t->kind,"float")||!strcmp(t->kind,"bool")||
        !strcmp(t->kind,"char")||!strcmp(t->kind,"void")) { nx(); return t->kind; }
    syn_fail = 1;
    snprintf(synmsg, SYNMSGLEN, "esperado tipo; encontrado %s", t->lexeme);
    return NULL;
}

static Node *parse_block(void);
static Node *parse_stmt(void);
static Node *parse_expr(void);

static Node *parse_primary(void) {
    CTok *t = nx();
    Node *nd;
    if (!strcmp(t->kind,"id")) {
        nd = newnode(N_VAR);
        snprintf(nd->lexeme, MAXLEX, "%s", t->lexeme);
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"int_lit")||!strcmp(t->kind,"real_lit")||!strcmp(t->kind,"char_lit")||
        !strcmp(t->kind,"string")||!strcmp(t->kind,"true")||!strcmp(t->kind,"false")) {
        const char *ty = !strcmp(t->kind,"int_lit") ? "int" :
                         !strcmp(t->kind,"real_lit") ? "float" :
                         (!strcmp(t->kind,"true")||!strcmp(t->kind,"false")) ? "bool" :
                         !strcmp(t->kind,"char_lit") ? "char" : "string";
        nd = newnode(N_LIT);
        snprintf(nd->type, 16, "%s", ty);
        snprintf(nd->lexeme, MAXLEX, "%s", t->lexeme);
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"(")) {
        Node *e = parse_expr();
        expect(")");
        return e;
    }
    syn_fail = 1;
    snprintf(synmsg, SYNMSGLEN, "expressão inválida em %s (linha %d, coluna %d)", t->lexeme, t->line, t->col);
    return NULL;
}

static Node *parse_postfix(void) {
    Node *e = parse_primary();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (!strcmp(t->kind,"[")) {
            nx();
            Node *idx = parse_expr();
            expect("]");
            Node *nd = newnode(N_INDEX);
            nd->a = e; nd->b = idx;
            nd->line = e->line; nd->col = e->col;
            e = nd;
        } else if (!strcmp(t->kind,"(")) {
            nx();
            Node *nd = newnode(N_CALL);
            nd->a = e;
            nd->line = e->line; nd->col = e->col;
            if (strcmp(pk()->kind,")") != 0) {
                for (;;) {
                    Node *arg = parse_expr();
                    if (arg && nd->nkids < MAXKIDS) nd->kids[nd->nkids++] = arg;
                    if (!accept(",")) break;
                }
            }
            expect(")");
            e = nd;
        } else return e;
    }
}

static Node *parse_unary(void) {
    CTok *t = pk();
    if (!strcmp(t->kind,"-") || !strcmp(t->kind,"!")) {
        nx();
        Node *operand = parse_unary();
        Node *nd = newnode(N_UN);
        snprintf(nd->op, 4, "%s", t->kind);
        nd->a = operand;
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    return parse_postfix();
}

static Node *mkbin(const char *op, Node *l, Node *r, CTok *op_tok) {
    Node *nd = newnode(N_BIN);
    snprintf(nd->op, 4, "%s", op);
    nd->a = l; nd->b = r;
    nd->line = l->line; nd->col = l->col;   /* início da expressão */
    nd->op_line = op_tok->line; nd->op_col = op_tok->col;
    return nd;
}

static Node *parse_mul(void) {
    Node *e = parse_unary();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"*") && strcmp(t->kind,"/") && strcmp(t->kind,"%")) return e;
        CTok *op = nx();
        e = mkbin(t->kind, e, parse_unary(), op);
    }
}

static Node *parse_add(void) {
    Node *e = parse_mul();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"+") && strcmp(t->kind,"-")) return e;
        CTok *op = nx();
        e = mkbin(t->kind, e, parse_mul(), op);
    }
}

static Node *parse_rel(void) {
    Node *e = parse_add();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"<") && strcmp(t->kind,">") && strcmp(t->kind,"<=") && strcmp(t->kind,">=")) return e;
        CTok *op = nx();
        e = mkbin(t->kind, e, parse_add(), op);
    }
}

static Node *parse_eq(void) {
    Node *e = parse_rel();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"==") && strcmp(t->kind,"!=")) return e;
        CTok *op = nx();
        e = mkbin(t->kind, e, parse_rel(), op);
    }
}

static Node *parse_and(void) {
    Node *e = parse_eq();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"&&")) return e;
        CTok *op = nx();
        e = mkbin("&&", e, parse_eq(), op);
    }
}

static Node *parse_or(void) {
    Node *e = parse_and();
    for (;;) {
        if (syn_fail || !e) return e;
        CTok *t = pk();
        if (strcmp(t->kind,"||")) return e;
        CTok *op = nx();
        e = mkbin("||", e, parse_and(), op);
    }
}

static Node *parse_assign(void) {
    Node *left = parse_or();
    if (syn_fail || !left) return left;
    if (!strcmp(pk()->kind,"=")) {
        nx();
        Node *right = parse_assign();
        Node *nd = newnode(N_ASSIGN);
        nd->a = left; nd->b = right;
        nd->line = left->line; nd->col = left->col;
        return nd;
    }
    return left;
}

static Node *parse_expr(void) { return parse_assign(); }

static int is_type_kw(const char *k) {
    return !strcmp(k,"int")||!strcmp(k,"float")||!strcmp(k,"bool")||!strcmp(k,"char");
}

static Node *parse_local_decl(void) {
    const char *ty = parse_type();
    Node *d = newnode(N_DECL);
    if (ty) snprintf(d->type, 16, "%s", ty);
    for (;;) {
        CTok *name = expect("id");
        if (d->ndecls < MAXDECLS) {
            Decl *de = &d->decls[d->ndecls++];
            snprintf(de->type, 16, "%s", ty ? ty : "?");
            snprintf(de->name, MAXLEX, "%s", name ? name->lexeme : "?");
            de->line = name ? name->line : 0;
            de->col  = name ? name->col  : 0;
            if (accept("[")) {
                de->is_array = 1;
                CTok *sz = expect("int_lit");
                if (sz) {
                    Node *szn = newnode(N_LIT);
                    snprintf(szn->type, 16, "int");
                    snprintf(szn->lexeme, MAXLEX, "%s", sz->lexeme);
                    szn->line = sz->line; szn->col = sz->col;
                    de->size = szn;
                }
                expect("]");
            } else if (accept("=")) {
                de->init = parse_expr();
            }
        }
        if (!accept(",")) break;
    }
    expect(";");
    return d;
}

static Node *parse_stmt(void) {
    CTok *t = pk();
    if (!strcmp(t->kind,"if")) {
        nx(); expect("(");
        Node *cond = parse_expr();
        expect(")");
        Node *then = parse_stmt();
        Node *els = accept("else") ? parse_stmt() : NULL;
        Node *nd = newnode(N_IF);
        nd->a = cond; nd->b = then; nd->c = els;
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"while")) {
        nx(); expect("(");
        Node *cond = parse_expr();
        expect(")");
        Node *nd = newnode(N_WHILE);
        nd->a = cond; nd->b = parse_stmt();
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"for")) {
        nx(); expect("(");
        Node *init = strcmp(pk()->kind,";")==0 ? NULL : parse_expr();
        expect(";");
        Node *cond = strcmp(pk()->kind,";")==0 ? NULL : parse_expr();
        expect(";");
        Node *upd  = strcmp(pk()->kind,")")==0 ? NULL : parse_expr();
        expect(")");
        Node *nd = newnode(N_FOR);
        nd->a = init; nd->b = cond; nd->c = upd; nd->body = parse_stmt();
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"return")) {
        nx();
        Node *expr = strcmp(pk()->kind,";")==0 ? NULL : parse_expr();
        expect(";");
        Node *nd = newnode(N_RETURN);
        nd->a = expr;
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"print")) {
        nx(); expect("(");
        Node *e = parse_expr();
        expect(")"); expect(";");
        Node *nd = newnode(N_PRINT);
        nd->a = e; nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"read")) {
        nx(); expect("(");
        Node *e = parse_expr();
        expect(")"); expect(";");
        Node *nd = newnode(N_READ);
        nd->a = e; nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"break")) {
        nx(); expect(";");
        Node *nd = newnode(N_BREAK);
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"continue")) {
        nx(); expect(";");
        Node *nd = newnode(N_CONTINUE);
        nd->line = t->line; nd->col = t->col;
        return nd;
    }
    if (!strcmp(t->kind,"{")) return parse_block();
    if (!strcmp(t->kind,";")) { nx(); return newnode(N_EMPTY); }
    Node *e = parse_expr();
    expect(";");
    Node *nd = newnode(N_EXPRSTMT);
    nd->a = e;
    nd->line = e ? e->line : t->line;
    nd->col  = e ? e->col  : t->col;
    return nd;
}

static Node *parse_block(void) {
    expect("{");
    Node *nd = newnode(N_BLOCK);
    while (strcmp(pk()->kind,"}") != 0 && !syn_fail) {
        Node *it = is_type_kw(pk()->kind) ? parse_local_decl() : parse_stmt();
        if (it && nd->nkids < MAXKIDS) nd->kids[nd->nkids++] = it;
    }
    expect("}");
    return nd;
}

static Node *parse_function(void) {
    const char *ret = parse_type();
    CTok *name = expect("id");
    expect("(");
    Node *f = newnode(N_FUNC);
    if (ret) snprintf(f->type, 16, "%s", ret);
    if (name) {
        snprintf(f->lexeme, MAXLEX, "%s", name->lexeme);
        f->line = name->line; f->col = name->col;
    }
    if (strcmp(pk()->kind,")") != 0) {
        for (;;) {
            const char *pty = parse_type();
            CTok *pn = expect("id");
            int parr = accept("[") != NULL;
            if (parr) expect("]");
            if (f->nparams < MAXPARAMS) {
                Param *pm = &f->params[f->nparams++];
                snprintf(pm->type, 16, "%s", pty ? pty : "?");
                snprintf(pm->name, MAXLEX, "%s", pn ? pn->lexeme : "?");
                pm->line = pn ? pn->line : 0;
                pm->col  = pn ? pn->col  : 0;
                pm->is_array = parr;
            }
            if (!accept(",")) break;
        }
    }
    expect(")");
    f->body = parse_block();
    return f;
}

static Node *parse_program(void) {
    Node *prog = newnode(N_PROGRAM);
    while (strcmp(pk()->kind,"eof") != 0 && !syn_fail) {
        Node *f = parse_function();
        if (f && prog->nkids < MAXKIDS) prog->kids[prog->nkids++] = f;
    }
    return prog;
}

/* ============================== 3. SEMÂNTICO ============================== */
typedef struct Sym {
    char name[MAXLEX];
    const char *cat;   /* "função" | "variável" | "vetor" | "parâmetro" */
    char type[16];
    int line, col, is_array;
    Param params[MAXPARAMS];
    int nparams;
    struct Sym *next;
} Sym;

static Sym *scopes[MAXSCOPES];
static int nscopes;
static Sym funcsyms[MAXFUNCS];
static int nfuncs;
static char *errs[MAXERRS];
static int nerrs;
static const char *cur_ret, *cur_fname;
static int loop_depth;

static void emit(const char *code, int line, int col, const char *fmt, ...) {
    if (nerrs >= MAXERRS) return;
    char msg[MSGLEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, MSGLEN, fmt, ap);
    va_end(ap);
    char *full = (char *)malloc(MSGLEN);
    snprintf(full, MSGLEN, "%s — linha %d, coluna %d: %s", code, line, col, msg);
    errs[nerrs++] = full;
}

static Sym *lookup(const char *name) {
    for (int s = nscopes - 1; s >= 0; s--)
        for (Sym *y = scopes[s]; y; y = y->next)
            if (!strcmp(y->name, name)) return y;
    return NULL;
}

static Sym *declare(const char *name, const char *cat, const char *type,
                    int line, int col, int is_array) {
    for (Sym *y = scopes[nscopes-1]; y; y = y->next) {
        if (!strcmp(y->name, name)) {
            emit("SEM002", line, col,
                 "“%s” já declarado neste escopo; declaração anterior na linha %d, coluna %d (tipo %s).",
                 name, y->line, y->col, y->type);
            return NULL;
        }
    }
    Sym *y = (Sym *)calloc(1, sizeof(Sym));
    snprintf(y->name, MAXLEX, "%s", name);
    y->cat = cat;
    snprintf(y->type, 16, "%s", type);
    y->line = line; y->col = col; y->is_array = is_array;
    y->next = scopes[nscopes-1];
    scopes[nscopes-1] = y;
    return y;
}

static int declare_function(const char *ret, const char *name, Param *params, int nparams,
                            int line, int col) {
    for (int i = 0; i < nfuncs; i++) {
        if (!strcmp(funcsyms[i].name, name)) {
            emit("SEM002", line, col,
                 "“%s” já declarado neste escopo; declaração anterior na linha %d, coluna %d (tipo %s).",
                 name, funcsyms[i].line, funcsyms[i].col, funcsyms[i].type);
            return 0;
        }
    }
    Sym *f = &funcsyms[nfuncs++];
    memset(f, 0, sizeof(Sym));
    snprintf(f->name, MAXLEX, "%s", name);
    f->cat = "função";
    snprintf(f->type, 16, "%s", ret);
    f->line = line; f->col = col;
    f->nparams = nparams;
    for (int i = 0; i < nparams; i++) f->params[i] = params[i];
    Sym *g = (Sym *)calloc(1, sizeof(Sym));
    snprintf(g->name, MAXLEX, "%s", name);
    g->cat = "função";
    snprintf(g->type, 16, "%s", ret);
    g->line = line; g->col = col;
    g->nparams = nparams;
    for (int i = 0; i < nparams; i++) g->params[i] = params[i];
    g->next = scopes[0];
    scopes[0] = g;
    return 1;
}

static char rbuf[4096];
static size_t rlen;

static void rputs(const char *s) {
    size_t l = strlen(s);
    if (rlen + l < sizeof(rbuf)) { memcpy(rbuf + rlen, s, l + 1); rlen += l; }
}

static void render_into(Node *e) {
    char tmp[16];
    if (!e) { rputs("?"); return; }
    switch (e->kind) {
    case N_LIT: case N_VAR:
        rputs(e->lexeme); break;
    case N_INDEX:
        render_into(e->a); rputs("["); render_into(e->b); rputs("]"); break;
    case N_CALL:
        render_into(e->a); rputs("(");
        for (int i = 0; i < e->nkids; i++) {
            if (i) rputs(", ");
            render_into(e->kids[i]);
        }
        rputs(")"); break;
    case N_ASSIGN:
        render_into(e->a); rputs(" = "); render_into(e->b); break;
    case N_BIN:
        render_into(e->a);
        snprintf(tmp, 16, " %s ", e->op); rputs(tmp);
        render_into(e->b); break;
    case N_UN:
        rputs(e->op); render_into(e->a); break;
    default:
        rputs("?"); break;
    }
}

static const char *render_expr(Node *e) {
    rlen = 0; rbuf[0] = 0;
    render_into(e);
    return rbuf;
}

static const char *describe_expr(Node *e, char *buf, size_t bl) {
    if (e->kind == N_LIT) {
        const char *nm =
            !strcmp(e->type,"int")   ? "o literal inteiro"   :
            !strcmp(e->type,"float") ? "o literal real"      :
            !strcmp(e->type,"bool")  ? "o literal booleano"  :
            !strcmp(e->type,"char")  ? "o literal caractere" : "a expressão";
        snprintf(buf, bl, "%s “%s”", nm, e->lexeme);
        return buf;
    }
    if (e->kind == N_CALL) {
        snprintf(buf, bl, "a chamada “%s”", e->a ? e->a->lexeme : "?");
        return buf;
    }
    snprintf(buf, bl, "a expressão “%s”", render_expr(e));
    return buf;
}

static int is_numeric(const char *t) {
    return !strcmp(t,"int") || !strcmp(t,"float") || !strcmp(t,"char");
}

static int convertible(const char *dst, const char *src) {
    if (!strcmp(dst, src)) return 1;
    if (!strcmp(dst,"float") && (!strcmp(src,"int") || !strcmp(src,"char"))) return 1;
    if (!strcmp(dst,"int") && !strcmp(src,"char")) return 1;
    return 0;
}

static const char *type_of(Node *e);

static void check_condition(Node *cond, const char *ctx) {
    const char *t = type_of(cond);
    if (t && strcmp(t,"bool") != 0)
        emit("SEM005", cond->line, cond->col,
             "Condição de %s deve ter tipo bool; recebeu %s (expressão “%s”).",
             ctx, t, render_expr(cond));
}

static void check_assign_compat(const char *dst, const char *dname, Node *value) {
    const char *vt = type_of(value);   /* espelha semantic.py: reavalia */
    if (vt && !convertible(dst, vt))
        emit("SEM003", value->line, value->col,
             "Não é possível atribuir %s a %s sem conversão permitida (destino “%s”; expressão “%s”).",
             vt, dst, dname, render_expr(value));
}

static const char *type_of(Node *e) {
    switch (e->kind) {
    case N_LIT:
        return e->type;
    case N_VAR: {
        Sym *y = lookup(e->lexeme);
        if (!y) {
            emit("SEM001", e->line, e->col,
                 "Identificador “%s” não declarado neste escopo.", e->lexeme);
            return NULL;
        }
        if (!strcmp(y->cat, "função")) {
            emit("SEM004", e->line, e->col,
                 "“%s” é uma função e não pode ser usada como valor.", e->lexeme);
            return NULL;
        }
        return y->type;
    }
    case N_INDEX: {
        Node *base = e->a, *idx = e->b;
        Sym *y = (base->kind == N_VAR) ? lookup(base->lexeme) : NULL;
        if (!y) {
            emit("SEM001", base->line, base->col,
                 "Identificador “%s” não declarado neste escopo.", base->lexeme);
        } else if (!y->is_array) {
            emit("SEM006", base->line, base->col, "“%s” não é um vetor.", base->lexeme);
        }
        const char *it = type_of(idx);
        if (it && strcmp(it,"int") != 0) {
            const char *vname = (base->kind == N_VAR) ? base->lexeme : "?";
            emit("SEM006", idx->line, idx->col,
                 "Índice do vetor “%s” deve ser int; recebeu %s (expressão “%s”).",
                 vname, it, render_expr(idx));
        }
        return y ? y->type : NULL;
    }
    case N_CALL: {
        Node *callee = e->a;
        if (callee->kind != N_VAR) {
            type_of(callee);
            for (int i = 0; i < e->nkids; i++) type_of(e->kids[i]);
            return NULL;
        }
        Sym *y = lookup(callee->lexeme);
        if (!y) {
            emit("SEM001", callee->line, callee->col,
                 "Identificador “%s” não declarado neste escopo.", callee->lexeme);
            for (int i = 0; i < e->nkids; i++) type_of(e->kids[i]);
            return NULL;
        }
        if (strcmp(y->cat, "função") != 0) {
            emit("SEM004", callee->line, callee->col, "“%s” não é uma função.", callee->lexeme);
            for (int i = 0; i < e->nkids; i++) type_of(e->kids[i]);
            return NULL;
        }
        if (e->nkids != y->nparams)
            emit("SEM007", e->line, e->col,
                 "“%s” espera %d argumentos, mas recebeu %d.",
                 callee->lexeme, y->nparams, e->nkids);
        for (int i = 0; i < e->nkids; i++) {
            const char *at = type_of(e->kids[i]);
            if (i < y->nparams && at && !convertible(y->params[i].type, at))
                emit("SEM008", e->kids[i]->line, e->kids[i]->col,
                     "Argumento %d de “%s”: esperado %s, recebido %s (expressão “%s”).",
                     i + 1, callee->lexeme, y->params[i].type, at, render_expr(e->kids[i]));
        }
        return y->type;
    }
    case N_ASSIGN: {
        Node *target = e->a, *value = e->b;
        const char *tt = type_of(target);
        if (target->kind != N_VAR && target->kind != N_INDEX) {
            char dbuf[512];
            emit("SEM013", target->line, target->col,
                 "Destino de atribuição não é atribuível; %s não designa uma variável ou elemento de vetor.",
                 describe_expr(target, dbuf, sizeof dbuf));
            type_of(value);
            return NULL;
        }
        if (!tt) { type_of(value); return NULL; }
        const char *vt = type_of(value);
        if (vt && value->kind == N_CALL && value->a && value->a->kind == N_VAR) {
            Sym *f = lookup(value->a->lexeme);
            if (f && !strcmp(f->cat,"função") && !strcmp(f->type,"void")) {
                emit("SEM012", value->line, value->col,
                     "Função “%s” não produz valor (retorno void) e não pode ser usada como expressão de atribuição.",
                     value->a->lexeme);
                return NULL;
            }
        }
        if (vt) {
            const char *dname = (target->kind == N_VAR) ? target->lexeme : target->a->lexeme;
            check_assign_compat(tt, dname, value);
        }
        return tt;
    }
    case N_BIN: {
        const char *op = e->op;
        const char *lt = type_of(e->a), *rt = type_of(e->b);
        if (!lt || !rt) return NULL;
        if (!strcmp(op,"&&") || !strcmp(op,"||")) {
            if (strcmp(lt,"bool") || strcmp(rt,"bool"))
                emit("SEM004", e->op_line, e->op_col,
                     "Operador “%s” exige operandos bool (recebeu %s e %s).", op, lt, rt);
            return "bool";
        }
        if (!strcmp(op,"==") || !strcmp(op,"!=")) {
            if (!((is_numeric(lt) && is_numeric(rt)) || !strcmp(lt,rt)))
                emit("SEM004", e->op_line, e->op_col,
                     "Operador “%s” não aceita operandos %s e %s.", op, lt, rt);
            return "bool";
        }
        if (!strcmp(op,"<") || !strcmp(op,">") || !strcmp(op,"<=") || !strcmp(op,">=")) {
            if (!is_numeric(lt) || !is_numeric(rt))
                emit("SEM004", e->op_line, e->op_col,
                     "Operador “%s” exige operandos numéricos (recebeu %s e %s).", op, lt, rt);
            return "bool";
        }
        if (!is_numeric(lt) || !is_numeric(rt)) {
            emit("SEM004", e->op_line, e->op_col,
                 "Operador “%s” exige operandos numéricos (recebeu %s e %s).", op, lt, rt);
            return NULL;
        }
        if (!strcmp(op,"%") && (strcmp(lt,"int") || strcmp(rt,"int"))) {
            emit("SEM004", e->op_line, e->op_col,
                 "Operador “%%” aceita apenas operandos int (recebeu %s e %s).", lt, rt);
            return NULL;
        }
        return (!strcmp(lt,"int") && !strcmp(rt,"int")) ? "int" : "float";
    }
    case N_UN: {
        const char *t = type_of(e->a);
        if (!t) return NULL;
        if (!strcmp(e->op,"!")) {
            if (strcmp(t,"bool"))
                emit("SEM004", e->line, e->col,
                     "Operador “!” exige operando bool (recebeu %s).", t);
            return "bool";
        }
        if (!is_numeric(t)) {
            emit("SEM004", e->line, e->col,
                 "Operador “-” exige operando numérico (recebeu %s).", t);
            return NULL;
        }
        return t;
    }
    default:
        return NULL;
    }
}

static void check_stmt(Node *st);

static int items_always_returns(Node **kids, int n);

static int stmt_always_returns(Node *st) {
    if (st->kind == N_RETURN) return 1;
    if (st->kind == N_IF && st->c) {
        Node *t = st->b, *el = st->c;
        int tr = (t->kind  == N_BLOCK) ? items_always_returns(t->kids,  t->nkids)  : stmt_always_returns(t);
        int er = (el->kind == N_BLOCK) ? items_always_returns(el->kids, el->nkids) : stmt_always_returns(el);
        return tr && er;
    }
    return 0;
}

static int items_always_returns(Node **kids, int n) {
    for (int i = 0; i < n; i++)
        if (kids[i] && stmt_always_returns(kids[i])) return 1;
    return 0;
}

static const char *fallthrough_reason(Node *body, char *buf, size_t bl) {
    for (int i = 0; i < body->nkids; i++) {
        Node *it = body->kids[i];
        if (it && it->kind == N_IF && !it->c) {
            Node *t = it->b;
            int tr = (t->kind == N_BLOCK) ? items_always_returns(t->kids, t->nkids)
                                          : stmt_always_returns(t);
            if (tr) {
                snprintf(buf, bl, "o ramo em que “%s” é falso alcança o fim do corpo.",
                         render_expr(it->a));
                return buf;
            }
        }
    }
    snprintf(buf, bl, "o fim do corpo é alcançável.");
    return buf;
}

static void check_item(Node *it) {
    switch (it->kind) {
    case N_DECL:
        for (int j = 0; j < it->ndecls; j++) {
            Decl *de = &it->decls[j];
            if (declare(de->name, de->is_array ? "vetor" : "variável", de->type,
                        de->line, de->col, de->is_array) && de->init)
                check_assign_compat(de->type, de->name, de->init);
        }
        break;
    case N_BLOCK:
        check_stmt(it);
        break;
    case N_IF:
        check_condition(it->a, "if");
        check_stmt(it->b);
        if (it->c) check_stmt(it->c);
        break;
    case N_WHILE:
        check_condition(it->a, "while");
        loop_depth++;
        check_stmt(it->b);
        loop_depth--;
        break;
    case N_FOR:
        if (it->a) type_of(it->a);
        if (it->b) check_condition(it->b, "for");
        if (it->c) type_of(it->c);
        loop_depth++;
        check_stmt(it->body);
        loop_depth--;
        break;
    case N_RETURN: {
        const char *ret = cur_ret;
        if (!it->a) {
            if (strcmp(ret,"void") != 0)
                emit("SEM009", it->line, it->col,
                     "Retorno sem valor em função “%s” de tipo %s.", cur_fname, ret);
        } else {
            const char *vt = type_of(it->a);
            if (!strcmp(ret,"void"))
                emit("SEM009", it->a->line, it->a->col,
                     "Retorno de valor em função “%s” de tipo void.", cur_fname);
            else if (vt && !convertible(ret, vt))
                emit("SEM009", it->a->line, it->a->col,
                     "Retorno %s incompatível com o tipo %s da função “%s”; conversão implícita de %s para %s não permitida.",
                     vt, ret, cur_fname, vt, ret);
        }
        break;
    }
    case N_PRINT:
        type_of(it->a);
        break;
    case N_READ: {
        Node *lv = it->a;
        type_of(lv);
        if (lv->kind != N_VAR && lv->kind != N_INDEX) {
            char dbuf[512];
            emit("SEM013", lv->line, lv->col,
                 "Destino de leitura não é atribuível; %s não designa uma variável ou elemento de vetor.",
                 describe_expr(lv, dbuf, sizeof dbuf));
        }
        break;
    }
    case N_BREAK: case N_CONTINUE:
        if (loop_depth == 0)
            emit("SEM010", it->line, it->col,
                 "“%s” fora de laço de repetição.",
                 it->kind == N_BREAK ? "break" : "continue");
        break;
    case N_EXPRSTMT:
        type_of(it->a);
        break;
    default:
        break;
    }
}

static void check_stmt(Node *st) {
    if (st->kind == N_BLOCK) {
        if (nscopes < MAXSCOPES) scopes[nscopes++] = NULL;
        for (int i = 0; i < st->nkids; i++)
            if (st->kids[i]) check_item(st->kids[i]);
        nscopes--;
    } else {
        check_item(st);
    }
}

static void check_function(const char *ret, const char *name, Param *params, int nparams,
                           Node *body, int decl_line) {
    if (nscopes < MAXSCOPES) scopes[nscopes++] = NULL;
    cur_ret = ret; cur_fname = name;
    loop_depth = 0;
    for (int i = 0; i < nparams; i++)
        declare(params[i].name, "parâmetro", params[i].type,
                params[i].line, params[i].col, params[i].is_array);
    for (int i = 0; i < body->nkids; i++)
        if (body->kids[i]) check_item(body->kids[i]);
    if (strcmp(ret,"void") != 0 && !items_always_returns(body->kids, body->nkids)) {
        char fr[512];
        emit("SEM011", decl_line, 1,
             "A função “%s” pode terminar sem retornar %s; %s",
             name, ret, fallthrough_reason(body, fr, sizeof fr));
    }
    nscopes--;
}

static void analyze_program(Node *prog) {
    nscopes = 1; scopes[0] = NULL;
    nfuncs = 0; nerrs = 0; loop_depth = 0;
    for (int i = 0; i < prog->nkids; i++) {
        Node *f = prog->kids[i];
        if (!f || f->kind != N_FUNC) continue;
        if (declare_function(f->type, f->lexeme, f->params, f->nparams, f->line, f->col))
            check_function(f->type, f->lexeme, f->params, f->nparams, f->body, f->line);
    }
}

/* ============================== 4. MAIN ============================== */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: minic <arquivo.c>\n");
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Erro: não foi possível abrir o arquivo: %s\n", argv[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)sz + 1);
    if (!src || fread(src, 1, (size_t)sz, fp) != (size_t)sz) {
        fprintf(stderr, "Erro: falha ao ler o arquivo\n");
        return 1;
    }
    src[sz] = 0;
    fclose(fp);

    if (!scan_src(src)) {
        fprintf(stderr, "Erro antes da análise semântica: %s\n", lexmsg);
        return 2;
    }
    tp = 0; syn_fail = 0;
    Node *prog = parse_program();
    if (syn_fail) {
        fprintf(stderr, "Erro antes da análise semântica: %s\n", synmsg);
        return 3;
    }
    analyze_program(prog);
    for (int i = 0; i < nerrs; i++) printf("%s\n", errs[i]);
    if (nerrs == 0) {
        printf("Análise semântica concluída: 0 erros; programa aceito.\n");
        return 0;
    }
    printf("Análise semântica concluída: %d erro%s; programa rejeitado.\n",
           nerrs, nerrs == 1 ? "" : "s");
    return 4;
}
