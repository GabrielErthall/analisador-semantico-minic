/* analisador_semantico.c — MINIC, componente isolado (versão C) — v4
 * Consome APENAS a AST produzida pelo parser (nunca tokens nem fonte).
 * Compilar junto ao restante do compilador: gcc -Wall -Wextra *.c -o compilador
 * Codificação: UTF-8 — as mensagens usam “” e — e são comparadas byte a byte
 * com os gabaritos da suíte de testes semânticos.
 *
 * Contrato da AST (equivale ao ast.h compartilhado com o parser):
 *   ND_PROGRAM   a = lista de ND_FUNC (encadeados por next)
 *   ND_FUNC      s1 = tipo de retorno, s2 = nome, a = lista de ND_VAR (params), b = corpo (ND_BLOCK)
 *   ND_VAR       s1 = tipo, s2 = nome (usado como parâmetro; is_arr marca vetor)
 *   ND_DECL      a = lista de declaradores (ND_VAR com s1=tipo, s2=nome, a=init, is_arr)
 *   ND_IF        a = cond, b = then, c = else (ou NULL)
 *   ND_WHILE     a = cond, b = corpo
 *   ND_FOR       a = init, b = cond, c = upd, d = corpo
 *   ND_RETURN    a = expr (ou NULL)
 *   ND_PRINT     a = expr    ND_READ a = expr
 *   ND_BREAK / ND_CONTINUE / ND_EMPTY
 *   ND_EXPRSTMT  a = expr
 *   ND_ASSIGN    a = alvo, b = valor
 *   ND_BIN       s1 = operador, a = esq, b = dir, line/col = operando esq, line2/col2 = operador
 *   ND_UN        s1 = operador, a = operando, line/col = operador
 *   ND_INDEX     a = base (ND_VAR), b = índice
 *   ND_CALL      s2 = nome, a = lista de args (encadeados por next), line/col = callee
 *   ND_VAR (expressão)  s2 = nome
 *   ND_LIT       s1 = tipo, s2 = lexema
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================
 * 1. CONTRATO DA AST
 * ============================================================ */
typedef enum {
    ND_PROGRAM, ND_FUNC, ND_BLOCK, ND_DECL, ND_IF, ND_WHILE, ND_FOR,
    ND_RETURN, ND_PRINT, ND_READ, ND_BREAK, ND_CONTINUE, ND_EXPRSTMT,
    ND_EMPTY, ND_ASSIGN, ND_BIN, ND_UN, ND_INDEX, ND_CALL, ND_VAR, ND_LIT
} NodeKind;

typedef struct Node {
    NodeKind kind;
    const char *s1, *s2;
    struct Node *a, *b, *c, *d;
    struct Node *next;
    int line, col, line2, col2;
    int is_arr;
} Node;

static Node *nd_new(NodeKind k, int line, int col) {
    Node *n = calloc(1, sizeof *n);
    if (!n) exit(1);
    n->kind = k; n->line = line; n->col = col;
    return n;
}

Node *nd_lit(const char *tipo, const char *lexema, int l, int c)
    { Node *n = nd_new(ND_LIT, l, c); n->s1 = tipo; n->s2 = lexema; return n; }
Node *nd_var(const char *nome, int l, int c)
    { Node *n = nd_new(ND_VAR, l, c); n->s2 = nome; return n; }
Node *nd_bin(const char *op, Node *esq, Node *dir, int ol, int oc) {
    Node *n = nd_new(ND_BIN, esq->line, esq->col);
    n->s1 = op; n->a = esq; n->b = dir; n->line2 = ol; n->col2 = oc;
    return n;
}
Node *nd_un(const char *op, Node *operando, int ol, int oc)
    { Node *n = nd_new(ND_UN, ol, oc); n->s1 = op; n->a = operando; return n; }
Node *nd_index(Node *base, Node *idx)
    { Node *n = nd_new(ND_INDEX, base->line, base->col); n->a = base; n->b = idx; return n; }
Node *nd_call(const char *nome, Node *args, int l, int c)
    { Node *n = nd_new(ND_CALL, l, c); n->s2 = nome; n->a = args; return n; }
Node *nd_assign(Node *alvo, Node *valor)
    { Node *n = nd_new(ND_ASSIGN, alvo->line, alvo->col); n->a = alvo; n->b = valor; return n; }
Node *nd_func(const char *ret, const char *nome, Node *params, Node *corpo, int l, int c)
    { Node *n = nd_new(ND_FUNC, l, c); n->s1 = ret; n->s2 = nome; n->a = params; n->b = corpo; return n; }
Node *nd_param(const char *tipo, const char *nome, int l, int c, int is_arr)
    { Node *n = nd_new(ND_VAR, l, c); n->s1 = tipo; n->s2 = nome; n->is_arr = is_arr; return n; }
Node *nd_declarator(const char *tipo, const char *nome, Node *init, int l, int c, int is_arr)
    { Node *n = nd_new(ND_VAR, l, c); n->s1 = tipo; n->s2 = nome; n->a = init; n->is_arr = is_arr; return n; }
Node *nd_decl(Node *declaradores, int l, int c)
    { Node *n = nd_new(ND_DECL, l, c); n->a = declaradores; return n; }
Node *nd_if(Node *cond, Node *then, Node *els)
    { Node *n = nd_new(ND_IF, cond->line, cond->col); n->a = cond; n->b = then; n->c = els; return n; }
Node *nd_while(Node *cond, Node *corpo)
    { Node *n = nd_new(ND_WHILE, cond->line, cond->col); n->a = cond; n->b = corpo; return n; }
Node *nd_for(Node *init, Node *cond, Node *upd, Node *corpo)
    { Node *n = nd_new(ND_FOR, corpo->line, corpo->col); n->a = init; n->b = cond; n->c = upd; n->d = corpo; return n; }
Node *nd_return(Node *expr, int l, int c)
    { Node *n = nd_new(ND_RETURN, l, c); n->a = expr; return n; }
Node *nd_block(Node *itens, int l, int c)
    { Node *n = nd_new(ND_BLOCK, l, c); n->a = itens; return n; }
Node *nd_break(int l, int c)    { return nd_new(ND_BREAK, l, c); }
Node *nd_continue(int l, int c) { return nd_new(ND_CONTINUE, l, c); }
Node *nd_empty(int l, int c)    { return nd_new(ND_EMPTY, l, c); }
Node *nd_print(Node *e)
    { Node *n = nd_new(ND_PRINT, e->line, e->col); n->a = e; return n; }
Node *nd_read(Node *e)
    { Node *n = nd_new(ND_READ, e->line, e->col); n->a = e; return n; }
Node *nd_exprstmt(Node *e)
    { Node *n = nd_new(ND_EXPRSTMT, e->line, e->col); n->a = e; return n; }
Node *nd_program(Node *funcs)
    { Node *n = nd_new(ND_PROGRAM, 1, 1); n->a = funcs; return n; }

/* ============================================================
 * 2. ESTADO DO ANALISADOR E UTILITÁRIOS
 * ============================================================ */
typedef struct Param   { char *name; const char *type; int line, col, is_arr; struct Param *next; } Param;
typedef struct FuncSym { char *name; const char *ret; Param *params; int line, col; struct FuncSym *next; } FuncSym;
typedef struct Sym     { char *name; const char *type; const char *cat; int line, col, is_arr; struct Sym *next; } Sym;
typedef struct Scope   { Sym *syms; struct Scope *prev; } Scope;

typedef struct Sem {
    Scope *scopes;
    FuncSym *funcs;
    const char *cur_ret, *cur_fn;
    int loop_depth;
    char **msgs; int nmsgs, cap;
} Sem;

static Sem G;

static char *xstrdup(const char *s) {
    char *p = malloc(strlen(s) + 1);
    if (p) strcpy(p, s);
    return p;
}

static void sem_err(Sem *S, const char *code, int line, int col,
                    const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (S->nmsgs == S->cap) {
        S->cap = S->cap ? S->cap * 2 : 16;
        S->msgs = realloc(S->msgs, (size_t)S->cap * sizeof *S->msgs);
    }
    char *m = malloc(1152);
    snprintf(m, 1152, "%s — linha %d, coluna %d: %s", code, line, col, buf);
    S->msgs[S->nmsgs++] = m;
}

typedef struct { char buf[1024]; size_t len; } SB;

static void sb_init(SB *sb) { sb->buf[0] = '\0'; sb->len = 0; }

static void sb_fmt(SB *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(sb->buf + sb->len, sizeof sb->buf - sb->len, fmt, ap);
    va_end(ap);
    if (w > 0 && sb->len + (size_t)w < sizeof sb->buf) sb->len += (size_t)w;
}

static const char *expr_type(Sem *S, Node *e);
static void check_stmt(Sem *S, Node *st);
static int always_returns_items(Node *items);
static int always_returns_block(Node *st);

static void render(Node *e, SB *sb) {
    switch (e->kind) {
    case ND_LIT: case ND_VAR: sb_fmt(sb, "%s", e->s2); break;
    case ND_INDEX:
        render(e->a, sb); sb_fmt(sb, "["); render(e->b, sb); sb_fmt(sb, "]");
        break;
    case ND_CALL:
        sb_fmt(sb, "%s(", e->s2);
        for (Node *a = e->a; a; a = a->next) {
            render(a, sb);
            if (a->next) sb_fmt(sb, ", ");
        }
        sb_fmt(sb, ")");
        break;
    case ND_ASSIGN:
        render(e->a, sb); sb_fmt(sb, " = "); render(e->b, sb); break;
    case ND_BIN:
        render(e->a, sb); sb_fmt(sb, " %s ", e->s1); render(e->b, sb); break;
    case ND_UN:
        sb_fmt(sb, "%s", e->s1); render(e->a, sb); break;
    default: sb_fmt(sb, "?");
    }
}

static void describe(Node *e, SB *sb) {
    if (e->kind == ND_LIT) {
        const char *t = !strcmp(e->s1, "int")   ? "o literal inteiro" :
                        !strcmp(e->s1, "float") ? "o literal real"    :
                        !strcmp(e->s1, "bool")  ? "o literal booleano" :
                                                  "o literal caractere";
        sb_fmt(sb, "%s “%s”", t, e->s2);
    } else if (e->kind == ND_CALL) {
        sb_fmt(sb, "a chamada “%s”", e->s2);
    } else {
        sb_fmt(sb, "a expressão “"); render(e, sb); sb_fmt(sb, "”");
    }
}

/* ============================================================
 * 3. TABELA DE SÍMBOLOS E REGRAS DE TIPO
 * ============================================================ */
static void open_scope(Sem *S) {
    Scope *sc = calloc(1, sizeof *sc);
    sc->prev = S->scopes;
    S->scopes = sc;
}

static void close_scope(Sem *S) { S->scopes = S->scopes->prev; }

static Sym *lookup(Sem *S, const char *name) {
    for (Scope *sc = S->scopes; sc; sc = sc->prev)
        for (Sym *s = sc->syms; s; s = s->next)
            if (!strcmp(s->name, name)) return s;
    return NULL;
}

static int declare(Sem *S, const char *name, const char *type,
                   const char *cat, int line, int col, int is_arr) {
    for (Sym *s = S->scopes->syms; s; s = s->next)
        if (!strcmp(s->name, name)) {
            sem_err(S, "SEM002", line, col,
                    "“%s” já declarado neste escopo; declaração anterior na "
                    "linha %d, coluna %d (tipo %s)",
                    name, s->line, s->col, s->type);
            return 0;
        }
    Sym *s = calloc(1, sizeof *s);
    s->name = xstrdup(name); s->type = type; s->cat = cat;
    s->line = line; s->col = col; s->is_arr = is_arr;
    s->next = S->scopes->syms;
    S->scopes->syms = s;
    return 1;
}

static void force_declare(Sem *S, const char *name, const char *type,
                          int line, int col) {
    Sym *s = calloc(1, sizeof *s);
    s->name = xstrdup(name); s->type = type; s->cat = "função";
    s->line = line; s->col = col;
    s->next = S->scopes->syms;
    S->scopes->syms = s;
}

static int convertible(const char *dst, const char *src) {
    if (!strcmp(dst, src)) return 1;
    if (!strcmp(dst, "float") && (!strcmp(src, "int") || !strcmp(src, "char")))
        return 1;
    if (!strcmp(dst, "int") && !strcmp(src, "char")) return 1;
    return 0;
}

static int numeric(const char *t) {
    return !strcmp(t, "int") || !strcmp(t, "float") || !strcmp(t, "char");
}

/* ============================================================
 * 4. VERIFICAÇÕES SEMÂNTICAS (SEM001–SEM013, gabarito da Aula 15)
 * ============================================================ */
static void check_assign_compat(Sem *S, const char *dst, const char *dname,
                                Node *value, const char *vt) {
    if (vt && !convertible(dst, vt)) {
        SB sb; sb_init(&sb); render(value, &sb);
        sem_err(S, "SEM003", value->line, value->col,
                "Não é possível atribuir %s a %s sem conversão permitida "
                "(destino “%s”; expressão “%s”).", vt, dst, dname, sb.buf);
    }
}

static void check_condition(Sem *S, Node *cond, const char *ctx) {
    const char *t = expr_type(S, cond);
    if (t && strcmp(t, "bool") != 0) {
        SB sb; sb_init(&sb); render(cond, &sb);
        sem_err(S, "SEM005", cond->line, cond->col,
                "Condição de %s deve ter tipo bool; recebeu %s (expressão “%s”).",
                ctx, t, sb.buf);
    }
}

static const char *expr_type(Sem *S, Node *e) {
    switch (e->kind) {
    case ND_LIT:
        return e->s1;
    case ND_VAR: {
        Sym *s = lookup(S, e->s2);
        if (!s) {
            sem_err(S, "SEM001", e->line, e->col,
                    "Identificador “%s” não declarado neste escopo.", e->s2);
            return NULL;
        }
        if (!strcmp(s->cat, "função")) {
            sem_err(S, "SEM004", e->line, e->col,
                    "“%s” é uma função e não pode ser usada como valor.", e->s2);
            return NULL;
        }
        return s->type;
    }
    case ND_INDEX: {
        Node *base = e->a, *idx = e->b;
        Sym *s = base->kind == ND_VAR ? lookup(S, base->s2) : NULL;
        if (!s)
            sem_err(S, "SEM001", base->line, base->col,
                    "Identificador “%s” não declarado neste escopo.",
                    base->s2 ? base->s2 : "?");
        else if (!s->is_arr)
            /* parâmetro-vetor (is_arr=1) é indexável — testes 04/07/10 */
            sem_err(S, "SEM006", base->line, base->col,
                    "“%s” não é um vetor.", base->s2);
        const char *it = expr_type(S, idx);
        if (it && strcmp(it, "int") != 0) {
            SB sb; sb_init(&sb); render(idx, &sb);
            sem_err(S, "SEM006", idx->line, idx->col,
                    "Índice do vetor “%s” deve ser int; recebeu %s "
                    "(expressão “%s”).", s ? base->s2 : "?", it, sb.buf);
        }
        return s ? s->type : NULL;
    }
    case ND_CALL: {
        Sym *s = lookup(S, e->s2);
        if (!s) {
            sem_err(S, "SEM001", e->line, e->col,
                    "Identificador “%s” não declarado neste escopo.", e->s2);
            for (Node *a = e->a; a; a = a->next) expr_type(S, a);
            return NULL;
        }
        if (strcmp(s->cat, "função") != 0) {
            sem_err(S, "SEM004", e->line, e->col,
                    "“%s” não é uma função.", e->s2);
            for (Node *a = e->a; a; a = a->next) expr_type(S, a);
            return NULL;
        }
        FuncSym *f = NULL;
        for (FuncSym *g = S->funcs; g; g = g->next)
            if (!strcmp(g->name, e->s2)) { f = g; break; }
        if (!f) return s->type;
        int nparams = 0, nargs = 0;
        for (Param *p = f->params; p; p = p->next) nparams++;
        for (Node *a = e->a; a; a = a->next) nargs++;
        if (nargs != nparams)
            sem_err(S, "SEM007", e->line, e->col,
                    "“%s” espera %d argumentos, mas recebeu %d.",
                    e->s2, nparams, nargs);
        int i = 0;
        Param *p = f->params;
        for (Node *a = e->a; a; a = a->next, i++, p = p ? p->next : NULL) {
            const char *at = expr_type(S, a);
            if (p && at && !convertible(p->type, at)) {
                SB sb; sb_init(&sb); render(a, &sb);
                sem_err(S, "SEM008", a->line, a->col,
                        "Argumento %d de “%s”: esperado %s, recebido %s "
                        "(expressão “%s”).", i + 1, e->s2, p->type, at, sb.buf);
            }
        }
        return s->type;
    }
    case ND_ASSIGN: {
        Node *target = e->a, *value = e->b;
        const char *tt = expr_type(S, target);
        if (target->kind != ND_VAR && target->kind != ND_INDEX) {
            SB sb; sb_init(&sb); describe(target, &sb);
            sem_err(S, "SEM013", target->line, target->col,
                    "Destino de atribuição não é atribuível; %s não designa "
                    "uma variável ou elemento de vetor.", sb.buf);
            expr_type(S, value);
            return NULL;
        }
        if (!tt) { expr_type(S, value); return NULL; }
        const char *vt = expr_type(S, value);
        if (vt && value->kind == ND_CALL) {
            Sym *fs = lookup(S, value->s2);
            if (fs && !strcmp(fs->cat, "função") && !strcmp(fs->type, "void")) {
                sem_err(S, "SEM012", value->line, value->col,
                        "Função “%s” não produz valor (retorno void) e não "
                        "pode ser usada como expressão de atribuição.", value->s2);
                return NULL;
            }
        }
        if (vt) {
            const char *dname = target->kind == ND_VAR ? target->s2 : target->a->s2;
            check_assign_compat(S, tt, dname, value, vt);
        }
        return tt;
    }
    case ND_BIN: {
        const char *op = e->s1;
        const char *lt = expr_type(S, e->a), *rt = expr_type(S, e->b);
        if (!lt || !rt) return NULL;
        int ol = e->line2, oc = e->col2;
        if (!strcmp(op, "&&") || !strcmp(op, "||")) {
            if (strcmp(lt, "bool") || strcmp(rt, "bool"))
                sem_err(S, "SEM004", ol, oc,
                        "Operador “%s” exige operandos bool (recebeu %s e %s).",
                        op, lt, rt);
            return "bool";
        }
        if (!strcmp(op, "==") || !strcmp(op, "!=")) {
            if (!((numeric(lt) && numeric(rt)) || !strcmp(lt, rt)))
                sem_err(S, "SEM004", ol, oc,
                        "Operador “%s” não aceita operandos %s e %s.", op, lt, rt);
            return "bool";
        }
        if (!strcmp(op, "<") || !strcmp(op, ">") ||
            !strcmp(op, "<=") || !strcmp(op, ">=")) {
            if (!numeric(lt) || !numeric(rt))
                sem_err(S, "SEM004", ol, oc,
                        "Operador “%s” exige operandos numéricos (recebeu %s e %s).",
                        op, lt, rt);
            return "bool";
        }
        if (!numeric(lt) || !numeric(rt)) {
            sem_err(S, "SEM004", ol, oc,
                    "Operador “%s” exige operandos numéricos (recebeu %s e %s).",
                    op, lt, rt);
            return NULL;
        }
        if (!strcmp(op, "%") && (strcmp(lt, "int") || strcmp(rt, "int"))) {
            sem_err(S, "SEM004", ol, oc,
                    "Operador “%%” aceita apenas operandos int (recebeu %s e %s).",
                    lt, rt);
            return NULL;
        }
        return (!strcmp(lt, "int") && !strcmp(rt, "int")) ? "int" : "float";
    }
    case ND_UN: {
        const char *t = expr_type(S, e->a);
        if (!t) return NULL;
        if (!strcmp(e->s1, "!")) {
            if (strcmp(t, "bool"))
                sem_err(S, "SEM004", e->line, e->col,
                        "Operador “!” exige operando bool (recebeu %s).", t);
            return "bool";
        }
        if (!numeric(t)) {
            sem_err(S, "SEM004", e->line, e->col,
                    "Operador “-” exige operando numérico (recebeu %s).", t);
            return NULL;
        }
        return t;
    }
    default:
        return NULL;
    }
}

static void check_item(Sem *S, Node *it) {
    switch (it->kind) {
    case ND_DECL:
        for (Node *d = it->a; d; d = d->next) {
            const char *cat = d->is_arr ? "vetor" : "variável";
            if (declare(S, d->s2, d->s1, cat, d->line, d->col, d->is_arr) && d->a) {
                const char *vt = expr_type(S, d->a);
                if (vt) check_assign_compat(S, d->s1, d->s2, d->a, vt);
            }
        }
        break;
    case ND_BLOCK:
        check_stmt(S, it);
        break;
    case ND_IF:
        check_condition(S, it->a, "if");
        check_stmt(S, it->b);
        if (it->c) check_stmt(S, it->c);
        break;
    case ND_WHILE:
        check_condition(S, it->a, "while");
        S->loop_depth++;
        check_stmt(S, it->b);
        S->loop_depth--;
        break;
    case ND_FOR:
        if (it->a) expr_type(S, it->a);
        if (it->b) check_condition(S, it->b, "for");
        if (it->c) expr_type(S, it->c);
        S->loop_depth++;
        check_stmt(S, it->d);
        S->loop_depth--;
        break;
    case ND_RETURN:
        if (!it->a) {
            if (strcmp(S->cur_ret, "void") != 0)
                sem_err(S, "SEM009", it->line, it->col,
                        "Retorno sem valor em função “%s” de tipo %s.",
                        S->cur_fn, S->cur_ret);
        } else {
            const char *vt = expr_type(S, it->a);
            if (!strcmp(S->cur_ret, "void"))
                sem_err(S, "SEM009", it->a->line, it->a->col,
                        "Retorno de valor em função “%s” de tipo void.", S->cur_fn);
            else if (vt && !convertible(S->cur_ret, vt))
                sem_err(S, "SEM009", it->a->line, it->a->col,
                        "Retorno %s incompatível com o tipo %s da função “%s”; "
                        "conversão implícita de %s para %s não permitida.",
                        vt, S->cur_ret, S->cur_fn, vt, S->cur_ret);
        }
        break;
    case ND_PRINT:
        expr_type(S, it->a);
        break;
    case ND_READ: {
        Node *lv = it->a;
        expr_type(S, lv);
        if (lv->kind != ND_VAR && lv->kind != ND_INDEX) {
            SB sb; sb_init(&sb); describe(lv, &sb);
            sem_err(S, "SEM013", lv->line, lv->col,
                    "Destino de leitura não é atribuível; %s não designa "
                    "uma variável ou elemento de vetor.", sb.buf);
        }
        break;
    }
    case ND_BREAK: case ND_CONTINUE:
        if (S->loop_depth == 0)
            sem_err(S, "SEM010", it->line, it->col,
                    "“%s” fora de laço de repetição.",
                    it->kind == ND_BREAK ? "break" : "continue");
        break;
    case ND_EXPRSTMT:
        expr_type(S, it->a);
        break;
    default:
        break;
    }
}

static void check_stmt(Sem *S, Node *st) {
    if (st->kind == ND_BLOCK) {
        open_scope(S);
        for (Node *i = st->a; i; i = i->next) check_item(S, i);
        close_scope(S);
    } else {
        check_item(S, st);
    }
}

static int always_returns_items(Node *items) {
    for (Node *it = items; it; it = it->next) {
        if (it->kind == ND_RETURN) return 1;
        if (it->kind == ND_IF && it->c &&
            always_returns_block(it->b) && always_returns_block(it->c))
            return 1;
    }
    return 0;
}

static int always_returns_block(Node *st) {
    if (st->kind == ND_BLOCK)
        return always_returns_items(st->a);
    return st->kind == ND_RETURN;
}

static void check_function(Sem *S, Node *fn) {
    open_scope(S);
    S->cur_ret = fn->s1;
    S->cur_fn = fn->s2;
    S->loop_depth = 0;
    for (Node *p = fn->a; p; p = p->next)
        declare(S, p->s2, p->s1, "parâmetro", p->line, p->col, p->is_arr);
    for (Node *i = fn->b->a; i; i = i->next) check_item(S, i);
    if (strcmp(fn->s1, "void") != 0 && !always_returns_items(fn->b->a)) {
        /* gabarito: SEM011 aponta para a linha da declaração, coluna 1 */
        const char *motivo = "o fim do corpo é alcançável.";
        SB sb; sb_init(&sb);
        for (Node *it = fn->b->a; it; it = it->next) {
            if (it->kind == ND_IF && !it->c && always_returns_block(it->b)) {
                render(it->a, &sb);
                motivo = NULL;
                break;
            }
        }
        if (motivo)
            sem_err(S, "SEM011", fn->line, 1,
                    "A função “%s” pode terminar sem retornar %s; %s",
                    fn->s2, fn->s1, motivo);
        else
            sem_err(S, "SEM011", fn->line, 1,
                    "A função “%s” pode terminar sem retornar %s; o ramo em que "
                    "“%s” é falso alcança o fim do corpo.",
                    fn->s2, fn->s1, sb.buf);
    }
    close_scope(S);
}

/* ============================================================
 * 5. INTERFACE PÚBLICA — chamadas esperadas do pipeline:
 *        int n = sem_analyze(ast);
 *        exit(sem_report());
 * ============================================================ */
int sem_analyze(Node *program) {
    memset(&G, 0, sizeof G);
    open_scope(&G);
    for (Node *f = program->a; f; f = f->next) {
        FuncSym *dup = NULL;
        for (FuncSym *g = G.funcs; g; g = g->next)
            if (!strcmp(g->name, f->s2)) { dup = g; break; }
        if (dup) {
            sem_err(&G, "SEM002", f->line, f->col,
                    "“%s” já declarado neste escopo; declaração anterior na "
                    "linha %d, coluna %d (tipo %s)",
                    f->s2, dup->line, dup->col, dup->ret);
            continue;
        }
        FuncSym *fs = calloc(1, sizeof *fs);
        fs->name = xstrdup(f->s2); fs->ret = f->s1;
        fs->line = f->line; fs->col = f->col;
        Param **tail = &fs->params;
        for (Node *p = f->a; p; p = p->next) {
            Param *q = calloc(1, sizeof *q);
            q->name = xstrdup(p->s2); q->type = p->s1;
            q->line = p->line; q->col = p->col; q->is_arr = p->is_arr;
            *tail = q; tail = &q->next;
        }
        fs->next = G.funcs; G.funcs = fs;
        force_declare(&G, f->s2, f->s1, f->line, f->col);
        check_function(&G, f);
    }
    return G.nmsgs;
}

int sem_report(void) {
    for (int i = 0; i < G.nmsgs; i++)
        printf("%s\n", G.msgs[i]);
    if (G.nmsgs == 0)
        printf("Análise semântica concluída: 0 erros; programa aceito.\n");
    else
        printf("Análise semântica concluída: %d erro%s; programa rejeitado.\n",
               G.nmsgs, G.nmsgs == 1 ? "" : "s");
    return G.nmsgs ? 4 : 0;
}