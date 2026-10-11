#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Analisador semântico da linguagem MINIC — versão Python (v6.2).

Arquivo único com as três fases claramente separadas (desacoplamento):
  1. SCANNER  — texto -> tokens (com linha/coluna)
  2. PARSER   — tokens -> AST
  3. SEMÂNTICO — AST -> diagnósticos (tabela de símbolos + tipos)

Uso:  python semantic.py <arquivo.c>
Saída: diagnósticos SEM001–SEM013 + linha final de conclusão.
Exit codes (Seção 11.1 da especificação): 0 aceito | 4 erro semântico.

Histórico de correções validadas pela execução real da suíte:
  - v5: método _error() definido; literais com kinds próprios (int_lit,
    real_lit, char_lit) para não colidir com palavras-chave de tipo;
  - v6: helper _pos() no semântico devolve (linha, coluna) conforme a
    tag do nó;
  - v6.1: SEM002 termina com ponto final, conforme gabarito;
  - v6.2: o parser passa a guardar a posição correta do INÍCIO da
    expressão nos nós compostos (bin, call, index, assign) via
    node_pos(). Antes copiava e[1]/e[2] do operando esquerdo, que para
    var/lit são nome/tipo (strings), causando TypeError nos casos
    16–18. Posições conferidas contra os gabaritos: SEM007 col. 12
    (nome da função), SEM008 col. 20/27 (início de cada argumento),
    SEM012 col. 9 (nome da função void).
"""
import sys

# =====================================================================
# 1. SCANNER
# =====================================================================
KEYWORDS = {"int", "float", "bool", "void", "if", "else", "while", "for",
            "return", "break", "continue", "print", "read", "true", "false"}
SIMPLE_OPS = set("+-*/%<>=!()[]{};,")


class Token:
    __slots__ = ("kind", "lexeme", "line", "col")

    def __init__(self, kind, lexeme, line, col):
        self.kind, self.lexeme, self.line, self.col = kind, lexeme, line, col


class LexicalError(Exception):
    def __init__(self, msg, line, col):
        super().__init__(msg)
        self.msg, self.line, self.col = msg, line, col


def tokenize(src):
    toks, i, line, col, n = [], 0, 1, 1, len(src)

    def advance(k):
        nonlocal i, line, col
        for _ in range(k):
            if i < n and src[i] == "\n":
                line += 1
                col = 1
            else:
                col += 1
            i += 1

    while i < n:
        c = src[i]
        if c in " \t\r\n":
            advance(1)
            continue
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                advance(1)
            continue
        if src.startswith("/*", i):
            sl, sc = line, col
            advance(2)
            while i < n and not src.startswith("*/", i):
                advance(1)
            if i >= n:
                raise LexicalError("comentário de bloco não terminado", sl, sc)
            advance(2)
            continue
        si, tl, tc = i, line, col
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            word = src[i:j]
            advance(j - i)
            toks.append(Token(word if word in KEYWORDS else "id", word, tl, tc))
            continue
        if c.isdigit():
            j = i
            while j < n and src[j].isdigit():
                j += 1
            if j < n and src[j] == "." and j + 1 < n and src[j + 1].isdigit():
                j += 1
                while j < n and src[j].isdigit():
                    j += 1
                advance(j - i)
                toks.append(Token("real_lit", src[si:i], tl, tc))
            else:
                advance(j - i)
                toks.append(Token("int_lit", src[si:i], tl, tc))
            continue
        if c == "'":
            advance(1)
            if i < n and src[i] == "\\":
                advance(2)
            elif i < n:
                advance(1)
            else:
                raise LexicalError("caractere não terminado", tl, tc)
            if i >= n or src[i] != "'":
                raise LexicalError("caractere com tamanho inválido", tl, tc)
            advance(1)
            toks.append(Token("char_lit", src[si:i], tl, tc))
            continue
        if c == '"':
            advance(1)
            while i < n and src[i] != '"':
                if src[i] == "\\" and i + 1 < n:
                    advance(2)
                else:
                    advance(1)
            if i >= n:
                raise LexicalError("cadeia não terminada", tl, tc)
            advance(1)
            toks.append(Token("string", src[si:i], tl, tc))
            continue
        two = src[i:i + 2]
        if two in ("==", "!=", "<=", ">=", "&&", "||"):
            advance(2)
            toks.append(Token(two, two, tl, tc))
            continue
        if c in SIMPLE_OPS:
            advance(1)
            toks.append(Token(c, c, tl, tc))
            continue
        raise LexicalError('símbolo "%s" não reconhecido' % c, tl, tc)
    toks.append(Token("eof", "", line, col))
    return toks


# =====================================================================
# 2. PARSER — gramática descendente, produz AST com posição de origem
# =====================================================================
class SyntaxErrorMinic(Exception):
    pass


def node_pos(e):
    """Posição (linha, coluna) do INÍCIO de um nó de expressão.
    Layout da AST (contrato parser -> semântico):
      lit   = ("lit", tipo, lexema, linha, col)              -> 3, 4
      var   = ("var", nome, linha, col)                      -> 2, 3
      bin   = ("bin", op, l, r, linha_ini, col_ini,
               linha_op, col_op)                             -> 4, 5
      un    = ("un", op, operando, linha_op, col_op)         -> 3, 4
      index = ("index", base, índice, linha_ini, col_ini)    -> 3, 4
      call  = ("call", callee, args, linha_ini, col_ini)     -> 3, 4
      assign= ("assign", destino, valor, linha_ini, col_ini) -> 3, 4
    """
    tag = e[0]
    if tag == "lit":
        return e[3], e[4]
    if tag == "var":
        return e[2], e[3]
    if tag == "bin":
        return e[4], e[5]
    if tag in ("call", "index", "assign", "un"):
        return e[3], e[4]
    return None


class Parser:
    def __init__(self, toks):
        self.toks, self.p = toks, 0

    def peek(self):
        return self.toks[self.p]

    def next(self):
        t = self.toks[self.p]
        self.p += 1
        return t

    def expect(self, kind):
        t = self.peek()
        if t.kind != kind:
            raise SyntaxErrorMinic(
                "esperado %s; encontrado %s (linha %d, coluna %d)"
                % (kind, t.lexeme or "fim de arquivo", t.line, t.col))
        return self.next()

    def accept(self, kind):
        if self.peek().kind == kind:
            return self.next()
        return None

    def parse_program(self):
        funcs = []
        while self.peek().kind != "eof":
            funcs.append(self.parse_function())
        return ("program", funcs)

    def parse_type(self):
        t = self.peek()
        if t.kind in ("int", "float", "bool", "char", "void"):
            self.next()
            return t.kind
        raise SyntaxErrorMinic("esperado tipo; encontrado %s" % t.lexeme)

    def parse_function(self):
        ret = self.parse_type()
        name = self.expect("id")
        self.expect("(")
        params = []
        if self.peek().kind != ")":
            while True:
                pt = self.parse_type()
                pn = self.expect("id")
                is_arr = bool(self.accept("["))
                if is_arr:
                    self.expect("]")
                params.append((pt, pn.lexeme, pn.line, pn.col, is_arr))
                if not self.accept(","):
                    break
        self.expect(")")
        body = self.parse_block()
        return ("func", ret, name.lexeme, params, body, name.line, name.col)

    def parse_block(self):
        self.expect("{")
        items = []
        while self.peek().kind != "}":
            if self.peek().kind in ("int", "float", "bool", "char"):
                items.append(self.parse_local_decl())
            else:
                items.append(self.parse_stmt())
        self.expect("}")
        return ("block", items)

    def parse_local_decl(self):
        ty = self.parse_type()
        decls = []
        while True:
            name = self.expect("id")
            is_arr, size, init = False, None, None
            if self.accept("["):
                is_arr = True
                size = self.expect("int_lit")
                self.expect("]")
            elif self.accept("="):
                init = self.parse_expr()
            decls.append((ty, name.lexeme, init, size, is_arr, name.line, name.col))
            if not self.accept(","):
                break
        self.expect(";")
        return ("decl", decls)

    def parse_stmt(self):
        t = self.peek()
        if t.kind == "if":
            self.next()
            self.expect("(")
            cond = self.parse_expr()
            self.expect(")")
            then = self.parse_stmt()
            els = self.parse_stmt() if self.accept("else") else None
            return ("if", cond, then, els, t.line, t.col)
        if t.kind == "while":
            self.next()
            self.expect("(")
            cond = self.parse_expr()
            self.expect(")")
            return ("while", cond, self.parse_stmt(), t.line, t.col)
        if t.kind == "for":
            self.next()
            self.expect("(")
            init = None if self.peek().kind == ";" else self.parse_expr()
            self.expect(";")
            cond = None if self.peek().kind == ";" else self.parse_expr()
            self.expect(";")
            upd = None if self.peek().kind == ")" else self.parse_expr()
            self.expect(")")
            return ("for", init, cond, upd, self.parse_stmt(), t.line, t.col)
        if t.kind == "return":
            self.next()
            expr = None if self.peek().kind == ";" else self.parse_expr()
            self.expect(";")
            return ("return", expr, t.line, t.col)
        if t.kind == "print":
            self.next()
            self.expect("(")
            e = self.parse_expr()
            self.expect(")")
            self.expect(";")
            return ("print", e, t.line, t.col)
        if t.kind == "read":
            self.next()
            self.expect("(")
            lv = self.parse_expr()
            self.expect(")")
            self.expect(";")
            return ("read", lv, t.line, t.col)
        if t.kind == "break":
            self.next()
            self.expect(";")
            return ("break", t.line, t.col)
        if t.kind == "continue":
            self.next()
            self.expect(";")
            return ("continue", t.line, t.col)
        if t.kind == "{":
            return self.parse_block()
        if t.kind == ";":
            self.next()
            return ("empty",)
        e = self.parse_expr()
        self.expect(";")
        return ("exprstmt", e)

    def parse_expr(self):
        return self.parse_assign()

    def parse_assign(self):
        left = self.parse_or()
        if self.peek().kind == "=":
            self.next()
            right = self.parse_assign()
            pl, pc = node_pos(left)
            return ("assign", left, right, pl, pc)
        return left

    def parse_or(self):
        e = self.parse_and()
        while self.peek().kind == "||":
            op = self.next()
            rhs = self.parse_and()
            pl, pc = node_pos(e)
            e = ("bin", "||", e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_and(self):
        e = self.parse_eq()
        while self.peek().kind == "&&":
            op = self.next()
            rhs = self.parse_eq()
            pl, pc = node_pos(e)
            e = ("bin", "&&", e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_eq(self):
        e = self.parse_rel()
        while self.peek().kind in ("==", "!="):
            op = self.next()
            rhs = self.parse_rel()
            pl, pc = node_pos(e)
            e = ("bin", op.kind, e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_rel(self):
        e = self.parse_add()
        while self.peek().kind in ("<", ">", "<=", ">="):
            op = self.next()
            rhs = self.parse_add()
            pl, pc = node_pos(e)
            e = ("bin", op.kind, e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_add(self):
        e = self.parse_mul()
        while self.peek().kind in ("+", "-"):
            op = self.next()
            rhs = self.parse_mul()
            pl, pc = node_pos(e)
            e = ("bin", op.kind, e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_mul(self):
        e = self.parse_unary()
        while self.peek().kind in ("*", "/", "%"):
            op = self.next()
            rhs = self.parse_unary()
            pl, pc = node_pos(e)
            e = ("bin", op.kind, e, rhs, pl, pc, op.line, op.col)
        return e

    def parse_unary(self):
        t = self.peek()
        if t.kind in ("-", "!"):
            self.next()
            return ("un", t.kind, self.parse_unary(), t.line, t.col)
        return self.parse_postfix()

    def parse_postfix(self):
        e = self.parse_primary()
        while True:
            t = self.peek()
            if t.kind == "[":
                self.next()
                idx = self.parse_expr()
                self.expect("]")
                pl, pc = node_pos(e)
                e = ("index", e, idx, pl, pc)
            elif t.kind == "(":
                self.next()
                args = []
                if self.peek().kind != ")":
                    while True:
                        args.append(self.parse_expr())
                        if not self.accept(","):
                            break
                self.expect(")")
                pl, pc = node_pos(e)
                e = ("call", e, args, pl, pc)
            else:
                return e

    def parse_primary(self):
        t = self.next()
        if t.kind == "id":
            return ("var", t.lexeme, t.line, t.col)
        if t.kind == "int_lit":
            return ("lit", "int", t.lexeme, t.line, t.col)
        if t.kind == "real_lit":
            return ("lit", "float", t.lexeme, t.line, t.col)
        if t.kind in ("true", "false"):
            return ("lit", "bool", t.lexeme, t.line, t.col)
        if t.kind == "char_lit":
            return ("lit", "char", t.lexeme, t.line, t.col)
        if t.kind == "string":
            return ("lit", "string", t.lexeme, t.line, t.col)
        if t.kind == "(":
            e = self.parse_expr()
            self.expect(")")
            return e
        raise SyntaxErrorMinic(
            "expressão inválida em %s (linha %d, coluna %d)" % (t.lexeme, t.line, t.col))


# =====================================================================
# 3. ANALISADOR SEMÂNTICO — tabela de símbolos, escopos e tipos
#    Mensagens canônicas conforme GABARITO.md da suíte oficial
#    (códigos SEM001–SEM013, aspas tipográficas, travessão).
# =====================================================================
class SemanticAnalyzer:
    NUMERIC = ("int", "float", "char")

    def __init__(self):
        self.errors = []

    def _error(self, code, line, col, msg):
        """Registra um diagnóstico no formato canônico do gabarito."""
        self.errors.append("%s — linha %d, coluna %d: %s" % (code, line, col, msg))

    @staticmethod
    def _pos(e):
        """Posição (linha, coluna) de um nó de expressão (ver node_pos)."""
        return node_pos(e)

    def analisar(self, ast):
        """Recebe a AST do parser e devolve a lista de diagnósticos."""
        _, funcs = ast
        self.scopes = [{}]
        self.funcs = {}
        for f in funcs:
            _, ret, name, params, body, fl, fc = f
            if not self._declare_function(ret, name, params, fl, fc):
                continue
            self._check_function(ret, name, params, body)
        return self.errors

    def _open_scope(self):
        self.scopes.append({})

    def _close_scope(self):
        self.scopes.pop()

    def _declare(self, name, info):
        cur = self.scopes[-1]
        if name in cur:
            prev = cur[name]
            self._error("SEM002", info["line"], info["col"],
                        "“%s” já declarado neste escopo; declaração anterior na "
                        "linha %d, coluna %d (tipo %s)."
                        % (name, prev["line"], prev["col"], prev["type"]))
            return False
        cur[name] = info
        return True

    def _lookup(self, name):
        for sc in reversed(self.scopes):
            if name in sc:
                return sc[name]
        return None

    def _declare_function(self, ret, name, params, fl, fc):
        if name in self.funcs:
            prev = self.funcs[name]
            self._error("SEM002", fl, fc,
                        "“%s” já declarado neste escopo; declaração anterior na "
                        "linha %d, coluna %d (tipo %s)."
                        % (name, prev[2], prev[3], prev[0]))
            return False
        self.funcs[name] = (ret, params, fl, fc)
        self.scopes[0][name] = {"cat": "função", "type": ret, "line": fl,
                                "col": fc, "params": params}
        return True

    def _check_function(self, ret, name, params, body):
        self._open_scope()
        self.current = (ret, name)
        self.loop_depth = 0
        for pt, pn, pl, pc, parr in params:
            self._declare(pn, {"cat": "parâmetro", "type": pt, "line": pl,
                               "col": pc, "array": parr})
        items = body[1]
        for it in items:
            self._check_item(it)
        if ret != "void" and not self._always_returns(items):
            # gabarito: SEM011 aponta para a linha da declaração, coluna 1
            self._error("SEM011", self.funcs[name][2], 1,
                        "A função “%s” pode terminar sem retornar %s; %s"
                        % (name, ret, self._fallthrough_reason(items)))
        self._close_scope()

    def _check_item(self, it):
        tag = it[0]
        if tag == "decl":
            for ty, name, init, size, is_arr, line, col in it[1]:
                if self._declare(name, {"cat": "vetor" if is_arr else "variável",
                                        "type": ty, "line": line, "col": col,
                                        "array": is_arr}) and init is not None:
                    self._check_assign_compat(ty, name, init)
        elif tag == "block":
            self._check_stmt(it)
        elif tag == "if":
            _, cond, then, els, _l, _c = it
            self._check_condition(cond, "if")
            self._check_stmt(then)
            if els is not None:
                self._check_stmt(els)
        elif tag == "while":
            _, cond, body, _l, _c = it
            self._check_condition(cond, "while")
            self.loop_depth += 1
            self._check_stmt(body)
            self.loop_depth -= 1
        elif tag == "for":
            _, init, cond, upd, body, _l, _c = it
            if init is not None:
                self.expr_type(init)
            if cond is not None:
                self._check_condition(cond, "for")
            if upd is not None:
                self.expr_type(upd)
            self.loop_depth += 1
            self._check_stmt(body)
            self.loop_depth -= 1
        elif tag == "return":
            _, expr, line, col = it
            ret, fname = self.current
            if expr is None:
                if ret != "void":
                    self._error("SEM009", line, col,
                                "Retorno sem valor em função “%s” de tipo %s."
                                % (fname, ret))
            else:
                vt = self.expr_type(expr)
                pl, pc = self._pos(expr)
                if ret == "void":
                    self._error("SEM009", pl, pc,
                                "Retorno de valor em função “%s” de tipo void." % fname)
                elif vt is not None and not self._convertible(ret, vt):
                    self._error("SEM009", pl, pc,
                                "Retorno %s incompatível com o tipo %s da função "
                                "“%s”; conversão implícita de %s para %s não permitida."
                                % (vt, ret, fname, vt, ret))
        elif tag == "print":
            self.expr_type(it[1])
        elif tag == "read":
            lv = it[1]
            self.expr_type(lv)
            if lv[0] not in ("var", "index"):
                pl, pc = self._pos(lv)
                self._error("SEM013", pl, pc,
                            "Destino de leitura não é atribuível; %s não designa "
                            "uma variável ou elemento de vetor." % self._describe(lv))
        elif tag in ("break", "continue"):
            if self.loop_depth == 0:
                self._error("SEM010", it[1], it[2],
                            "“%s” fora de laço de repetição." % tag)
        elif tag == "exprstmt":
            self.expr_type(it[1])

    def _check_stmt(self, st):
        if st[0] == "block":
            self._open_scope()
            for it in st[1]:
                self._check_item(it)
            self._close_scope()
        else:
            self._check_item(st)

    def _check_condition(self, cond, ctx):
        t = self.expr_type(cond)
        if t is not None and t != "bool":
            pl, pc = self._pos(cond)
            self._error("SEM005", pl, pc,
                        "Condição de %s deve ter tipo bool; recebeu %s "
                        "(expressão “%s”)." % (ctx, t, self._render(cond)))

    def expr_type(self, e):
        tag = e[0]
        if tag == "lit":
            return e[1]
        if tag == "var":
            info = self._lookup(e[1])
            if info is None:
                self._error("SEM001", e[2], e[3],
                            "Identificador “%s” não declarado neste escopo." % e[1])
                return None
            if info["cat"] == "função":
                self._error("SEM004", e[2], e[3],
                            "“%s” é uma função e não pode ser usada como valor." % e[1])
                return None
            return info["type"]
        if tag == "index":
            base, idx = e[1], e[2]
            info = self._lookup(base[1]) if base[0] == "var" else None
            if info is None:
                pl, pc = self._pos(base)
                self._error("SEM001", pl, pc,
                            "Identificador “%s” não declarado neste escopo." % base[1])
            elif not info.get("array"):
                # vale para variável escalar; parâmetro-vetor (array=True) é
                # indexável — exigido pelos casos 04/07/10
                self._error("SEM006", base[2], base[3],
                            "“%s” não é um vetor." % base[1])
            it = self.expr_type(idx)
            if it is not None and it != "int":
                pl, pc = self._pos(idx)
                self._error("SEM006", pl, pc,
                            "Índice do vetor “%s” deve ser int; recebeu %s "
                            "(expressão “%s”)."
                            % (base[1] if base[0] == "var" else "?", it,
                               self._render(idx)))
            return info["type"] if info else None
        if tag == "call":
            callee, args = e[1], e[2]
            if callee[0] != "var":
                self.expr_type(callee)
                for a in args:
                    self.expr_type(a)
                return None
            info = self._lookup(callee[1])
            if info is None:
                self._error("SEM001", callee[2], callee[3],
                            "Identificador “%s” não declarado neste escopo." % callee[1])
                for a in args:
                    self.expr_type(a)
                return None
            if info["cat"] != "função":
                self._error("SEM004", callee[2], callee[3],
                            "“%s” não é uma função." % callee[1])
                for a in args:
                    self.expr_type(a)
                return None
            params = info["params"]
            if len(args) != len(params):
                # gabarito: posição do nome da função chamada (teste 16:
                # linha 5, coluna 12)
                self._error("SEM007", e[3], e[4],
                            "“%s” espera %d argumentos, mas recebeu %d."
                            % (callee[1], len(params), len(args)))
            for i, a in enumerate(args):
                at = self.expr_type(a)
                if i < len(params) and at is not None:
                    pt = params[i][0]
                    if not self._convertible(pt, at):
                        # gabarito: posição do INÍCIO do argumento
                        # (teste 17: colunas 20 e 27)
                        pl, pc = self._pos(a)
                        self._error("SEM008", pl, pc,
                                    "Argumento %d de “%s”: esperado %s, recebido %s "
                                    "(expressão “%s”)."
                                    % (i + 1, callee[1], pt, at, self._render(a)))
            return info["type"]
        if tag == "assign":
            _, target, value, tl, tc = e
            tt = self.expr_type(target)
            if target[0] not in ("var", "index"):
                pl, pc = self._pos(target)
                self._error("SEM013", pl, pc,
                            "Destino de atribuição não é atribuível; %s não designa "
                            "uma variável ou elemento de vetor." % self._describe(target))
                self.expr_type(value)
                return None
            if tt is None:
                self.expr_type(value)
                return None
            vt = self.expr_type(value)
            if vt is not None and value[0] == "call" and value[1][0] == "var":
                finfo = self._lookup(value[1][1])
                if finfo and finfo["cat"] == "função" and finfo["type"] == "void":
                    # gabarito: posição do nome da função chamada
                    # (teste 18: linha 6, coluna 9)
                    self._error("SEM012", value[3], value[4],
                                "Função “%s” não produz valor (retorno void) e não "
                                "pode ser usada como expressão de atribuição."
                                % value[1][1])
                    return None
            if vt is not None:
                dname = target[1] if target[0] == "var" else target[1][1]
                self._check_assign_compat(tt, dname, value)
            return tt
        if tag == "bin":
            _, op, l, r, _sl, _sc, ol, ocl = e
            lt, rt = self.expr_type(l), self.expr_type(r)
            if lt is None or rt is None:
                return None
            if op in ("&&", "||"):
                if lt != "bool" or rt != "bool":
                    self._error("SEM004", ol, ocl,
                                "Operador “%s” exige operandos bool (recebeu %s e %s)."
                                % (op, lt, rt))
                return "bool"
            if op in ("==", "!="):
                if not (lt in self.NUMERIC and rt in self.NUMERIC or lt == rt):
                    self._error("SEM004", ol, ocl,
                                "Operador “%s” não aceita operandos %s e %s."
                                % (op, lt, rt))
                return "bool"
            if op in ("<", ">", "<=", ">="):
                if lt not in self.NUMERIC or rt not in self.NUMERIC:
                    self._error("SEM004", ol, ocl,
                                "Operador “%s” exige operandos numéricos "
                                "(recebeu %s e %s)." % (op, lt, rt))
                return "bool"
            if lt not in self.NUMERIC or rt not in self.NUMERIC:
                self._error("SEM004", ol, ocl,
                            "Operador “%s” exige operandos numéricos "
                            "(recebeu %s e %s)." % (op, lt, rt))
                return None
            if op == "%" and not (lt == "int" and rt == "int"):
                self._error("SEM004", ol, ocl,
                            "Operador “%%” aceita apenas operandos int "
                            "(recebeu %s e %s)." % (lt, rt))
                return None
            return "int" if (lt == "int" and rt == "int") else "float"
        if tag == "un":
            _, op, operand, ul, ucl = e
            t = self.expr_type(operand)
            if t is None:
                return None
            if op == "!":
                if t != "bool":
                    self._error("SEM004", ul, ucl,
                                "Operador “!” exige operando bool (recebeu %s)." % t)
                return "bool"
            if t not in self.NUMERIC:
                self._error("SEM004", ul, ucl,
                            "Operador “-” exige operando numérico (recebeu %s)." % t)
                return None
            return t
        return None

    @staticmethod
    def _convertible(dst, src):
        if dst == src:
            return True
        if dst == "float" and src in ("int", "char"):
            return True
        if dst == "int" and src == "char":
            return True
        return False

    def _check_assign_compat(self, dst, dname, value):
        vt = self.expr_type(value)
        if vt is not None and not self._convertible(dst, vt):
            pl, pc = self._pos(value)
            self._error("SEM003", pl, pc,
                        "Não é possível atribuir %s a %s sem conversão permitida "
                        "(destino “%s”; expressão “%s”)."
                        % (vt, dst, dname, self._render(value)))

    def _describe(self, e):
        if e[0] == "lit":
            names = {"int": "o literal inteiro", "float": "o literal real",
                     "bool": "o literal booleano", "char": "o literal caractere"}
            return "%s “%s”" % (names.get(e[1], "a expressão"), e[2])
        if e[0] == "call":
            return "a chamada “%s”" % e[1][1]
        return "a expressão “%s”" % self._render(e)

    @classmethod
    def _render(cls, e):
        tag = e[0]
        if tag == "lit":
            return e[2]
        if tag == "var":
            return e[1]
        if tag == "index":
            return "%s[%s]" % (cls._render(e[1]), cls._render(e[2]))
        if tag == "call":
            return "%s(%s)" % (cls._render(e[1]),
                               ", ".join(cls._render(a) for a in e[2]))
        if tag == "assign":
            return "%s = %s" % (cls._render(e[1]), cls._render(e[2]))
        if tag == "bin":
            return "%s %s %s" % (cls._render(e[2]), e[1], cls._render(e[3]))
        if tag == "un":
            return "%s%s" % (e[1], cls._render(e[2]))
        return "?"

    def _always_returns(self, items):
        for it in items:
            if it[0] == "return":
                return True
            if it[0] == "if":
                _, _cond, then, els, _l, _c = it
                then_items = then[1] if then[0] == "block" else [then]
                if els is not None:
                    else_items = els[1] if els[0] == "block" else [els]
                    if self._always_returns(then_items) and \
                            self._always_returns(else_items):
                        return True
        return False

    def _fallthrough_reason(self, items):
        for it in items:
            if it[0] == "if":
                _, cond, then, els, _l, _c = it
                then_items = then[1] if then[0] == "block" else [then]
                if els is None and self._always_returns(then_items):
                    return ("o ramo em que “%s” é falso alcança o fim do corpo."
                            % self._render(cond))
        return "o fim do corpo é alcançável."


# =====================================================================
# 4. PIPELINE — execução e saída canônica (exit code 0/4)
# =====================================================================
def main():
    if len(sys.argv) != 2:
        print("Uso: python semantic.py <arquivo.c>", file=sys.stderr)
        sys.exit(1)
    try:
        with open(sys.argv[1], encoding="utf-8") as f:
            src = f.read()
    except OSError as exc:
        print("Erro: não foi possível abrir o arquivo: %s" % exc, file=sys.stderr)
        sys.exit(1)
    try:
        toks = tokenize(src)
        ast = Parser(toks).parse_program()
    except (LexicalError, SyntaxErrorMinic) as exc:
        print("Erro antes da análise semântica: %s" % exc, file=sys.stderr)
        sys.exit(2 if isinstance(exc, LexicalError) else 3)
    sem = SemanticAnalyzer()
    msgs = sem.analisar(ast)
    for m in msgs:
        print(m)
    n = len(msgs)
    if n == 0:
        print("Análise semântica concluída: 0 erros; programa aceito.")
        sys.exit(0)
    print("Análise semântica concluída: %d erro%s; programa rejeitado."
          % (n, "" if n == 1 else "s"))
    sys.exit(4)


if __name__ == "__main__":
    main()
