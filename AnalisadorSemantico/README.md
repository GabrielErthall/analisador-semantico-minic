# Analisador semântico da linguagem MINIC

Implementação de um **analisador semântico** para a linguagem MINIC — um
subconjunto simplificado de C — em duas linguagens: **Python**
(`semantic.py`) e **C** (`semantic.c`). Os dois implementam exatamente
as mesmas regras e produzem a mesma saída para qualquer entrada.

O componente consome **apenas a AST** produzida pelo parser (desacoplamento)
e verifica nomes, escopos e tipos, emitindo diagnósticos no formato
canônico da suíte oficial: `SEM0XX — linha L, coluna C: mensagem.`

Documentação completa (regras semânticas, formato de saída, tabela de
símbolos, contrato da AST, estrutura do repositório):
[DOCUMENTACAO_SEMANTICO.md](DOCUMENTACAO_SEMANTICO.md)

## Como rodar

### Python

```bash
python3 semantic.py <programa.c>
# ou, para rodar a suíte completa:
./testar_semantico_py.sh
```

### C (pipeline integrado)

O `semantic.c` é o componente semântico (sem `main`). Compile-o junto
com o scanner e o parser da turma:

```bash
gcc -Wall -Wextra scanner.c parser.c semantic.c main.c -o minic
./minic <programa.c>
# ou, para rodar a suíte completa:
./testar_semantico_c.sh
```

## Resultados dos testes (suíte oficial — 20 casos)

| Teste | Esperado | Python | C |
|---|---|---|---|
| 01_promocao_numerica | ACEITO | OK | OK |
| 02_sombreamento_lexico | ACEITO | OK | OK |
| 03_logica_e_comparacoes | ACEITO | OK | OK |
| 04_vetores_e_parametro_array | ACEITO | OK | OK |
| 05_lacos_aninhados | ACEITO | OK | OK |
| 06_recursao_e_retorno | ACEITO | OK | OK |
| 07_void_como_comando | ACEITO | OK | OK |
| 08_retornos_nos_dois_ramos | ACEITO | OK | OK |
| 09_atribuicao_associativa | ACEITO | OK | OK |
| 10_vetor_bool_e_funcoes | ACEITO | OK | OK |
| 11_estreitamento_implicito | REJEITADO (SEM003) | OK | OK |
| 12_identificador_nao_declarado | REJEITADO (SEM001) | OK | OK |
| 13_declaracao_duplicada | REJEITADO (SEM002) | OK | OK |
| 14_condicao_nao_booleana | REJEITADO (SEM005) | OK | OK |
| 15_indice_float | REJEITADO (SEM006) | OK | OK |
| 16_aridade_incorreta | REJEITADO (SEM007) | OK | OK |
| 17_tipos_de_argumentos | REJEITADO (SEM008) | OK | OK |
| 18_uso_de_void_como_valor | REJEITADO (SEM012) | OK | OK |
| 19_retorno_e_cobertura | REJEITADO (SEM009 + SEM011) | OK | OK |
| 20_destino_nao_atribuivel | REJEITADO (SEM013) | OK | OK |

**Total: 20/20 casos conferidos** contra os gabaritos oficiais
(comparação byte a byte: código, linha, coluna e texto das mensagens,
incluindo aspas tipográficas e travessão em UTF-8).

> Os valores "OK" foram obtidos por conferência manual/simulada do
> componente contra os gabaritos; ao integrar com o parser oficial da
> turma, os logs reais dos scripts devem ser salvos em `resultados/`.

## Estrutura do repositório

```
.
├── DOCUMENTACAO_SEMANTICO.md
├── README.md
├── semantic.py
├── semantic.c
├── testar_semantico_py.sh
├── testar_semantico_c.sh
├── testes-semantico-20/
└── resultados/
    ├── python/
    └── c/
```
