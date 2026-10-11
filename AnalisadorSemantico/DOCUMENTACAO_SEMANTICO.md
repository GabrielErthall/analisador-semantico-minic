# Documentação do analisador semântico da linguagem MINIC

Este repositório contém a implementação de um **analisador semântico**
para a linguagem MINIC — um subconjunto simplificado de C — em duas
linguagens: **Python** (`semantic.py`) e **C** (`semantic.c`). Os dois
implementam exatamente as mesmas regras e produzem a mesma saída para
qualquer entrada (código, linha, coluna e texto das mensagens).

## Papel do componente no compilador

O analisador semântico é a terceira fase do pipeline. Ele consome
**apenas a AST** produzida pelo parser — nunca tokens nem o texto-fonte
— e valida nomes, escopos e tipos conforme a especificação do MINIC
(Seções 5 a 7) e as mensagens canônicas da suíte oficial de testes.

```
   fonte ──> scanner ──> parser ──> AST ──> ANALISADOR SEMÂNTICO ──> diagnóstico / exit code
```

## Como executar

### Versão Python (arquivo único, autocontido)

```bash
python3 semantic.py <programa.c>
```

### Versão C (componente + pipeline)

O `semantic.c` é o componente semântico propriamente dito (sem `main`).
Para executar, compile-o junto com o scanner e o parser da turma:

```bash
gcc -Wall -Wextra scanner.c parser.c semantic.c main.c -o minic
./minic <programa.c>
```

## Formato da saída

Cada diagnóstico ocupa uma linha, no formato canônico da suíte oficial:

```
SEM0XX — linha L, coluna C: mensagem.
```

A execução termina sempre com uma linha de conclusão:

```
Análise semântica concluída: 0 erros; programa aceito.
Análise semântica concluída: N erro(s); programa rejeitado.
```

Códigos de saída (Seção 11.1 da especificação): `0` para programa
aceito, `4` para erro semântico, `1` uso inválido, `2` erro léxico,
`3` erro sintático.

## Regras semânticas verificadas (SEM001–SEM013)

| Código | Regra | Mensagem canônica |
|---|---|---|
| SEM001 | Identificador não declarado | `Identificador “x” não declarado neste escopo.` |
| SEM002 | Declaração duplicada | `“x” já declarado neste escopo; declaração anterior na linha L, coluna C (tipo t).` |
| SEM003 | Incompatibilidade de atribuição | `Não é possível atribuir float a int sem conversão permitida (destino “n”; expressão “2.5”).` |
| SEM004 | Operando inválido | `Operador “op” exige operandos ... (recebeu ...).` |
| SEM005 | Condição não booleana | `Condição de if deve ter tipo bool; recebeu int (expressão “n”).` |
| SEM006 | Índice de vetor inválido | `Índice do vetor “v” deve ser int; recebeu float (expressão “1.0”).` |
| SEM007 | Aridade incorreta | `“f” espera N argumentos, mas recebeu M.` |
| SEM008 | Tipo de argumento incompatível | `Argumento k de “f”: esperado t, recebido u (expressão “e”).` |
| SEM009 | Retorno incompatível | `Retorno float incompatível com o tipo int da função “f”; conversão implícita de float para int não permitida.` |
| SEM010 | break/continue fora de laço | `“break” fora de laço de repetição.` |
| SEM011 | Queda de função não void | `A função “f” pode terminar sem retornar t; o ramo em que “cond” é falso alcança o fim do corpo.` |
| SEM012 | Chamada void como valor | `Função “f” não produz valor (retorno void) e não pode ser usada como expressão de atribuição.` |
| SEM013 | Destino não atribuível | `Destino de atribuição não é atribuível; o literal inteiro “3” não designa uma variável ou elemento de vetor.` |

> SEM004 e SEM010 não aparecem na suíte oficial de 20 casos; seus textos
> seguem o mesmo padrão das demais e podem ser ajustados conforme a
> apostila da Aula 15.

## Conversões implícitas permitidas

| Origem → Destino | Permitido? |
|---|---|
| int → float | sim |
| char → int | sim |
| char → float | sim |
| float → int | **não** (estrita) |
| bool → int / int → bool / float → bool | **não** (estrita) |

O operador `%` aceita apenas operandos `int`. Operadores relacionais
retornam `bool`; operadores lógicos (`&&`, `||`, `!`) exigem operandos
`bool`. Condições de `if`, `while` e `for` devem ter tipo `bool`.

## Tabela de símbolos e escopos

Implementação com pilha de escopos léxicos:

- `abrir_escopo()` / `fechar_escopo()` — empilha/desempilha;
- `inserir(nome, info)` — falha com SEM002 se o nome já existe **no escopo corrente**;
- `buscar(nome)` — procura do escopo corrente até o global;
- funções são inseridas no escopo global e declaradas **antes do uso**;
- sombreamento léxico (mesmo nome em escopos diferentes) é permitido —
  conforme o caso oficial `02_sombreamento_lexico`.

## Interface interna (desacoplamento)

**Python**: `SemanticAnalyzer().analisar(ast) -> list[str]`. O contrato
da AST está documentado no docstring de `semantic.py` (tuplas com tag,
campos de linha/coluna em cada nó).

**C**: `int sem_analyze(Node *program)` devolve o número de erros;
`int sem_report()` imprime os diagnósticos, a linha final e devolve o
código de saída (0 ou 4). O contrato da AST está no cabeçalho do
`semantic.c` (enum `NodeKind` + struct `Node`).

O componente não acessa tokens nem o fonte em nenhum momento — a única
dependência é a estrutura da AST, que é o contrato entre parser e
analisador semântico.

## Estrutura do repositório

```
.
├── DOCUMENTACAO_SEMANTICO.md   este arquivo
├── README.md                   visão geral e resultados
├── semantic.py                 implementação em Python (autocontida)
├── semantic.c                  componente semântico em C
├── testar_semantico_py.sh      script de teste da versão Python
├── testar_semantico_c.sh       script de teste da versão C
├── testes-semantico-20/        suíte oficial do professor (20 casos)
│   ├── 01_promocao_numerica.c ... 20_destino_nao_atribuivel.c
│   └── *.gabarito              saída esperada de cada caso
└── resultados/
    ├── python/                 saídas reais da versão Python
    └── c/                      saídas reais da versão C
```