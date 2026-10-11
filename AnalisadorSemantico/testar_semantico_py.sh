#!/usr/bin/env bash
# Testa o analisador semântico Python (semantic.py) contra a suíte de
# 20 casos semânticos do MINIC (testes-semantico-20), no mesmo formato
# do script oficial do professor (comparação byte a byte com diff).
#
# Uso:
#   ./testar_semantico_py.sh [analisador.py] [pasta-de-testes]
# Exemplo:
#   ./testar_semantico_py.sh ./semantic.py ./testes-semantico-20
#
# O analisador deve aceitar:
#   python3 analisador.py arquivo.c
# A saída padrão (com stderr incluído) é comparada byte a byte com o
# arquivo <entrada>.gabarito. Codificação esperada: UTF-8.

set -u

arquivo_fonte=${1:-./semantic.py}
diretorio=${2:-./testes-semantico-20}

if [[ ! -f "$arquivo_fonte" ]]; then
  echo "Erro: analisador não encontrado: $arquivo_fonte" >&2
  exit 2
fi
if [[ ! -d "$diretorio" ]]; then
  echo "Erro: diretório de testes não encontrado: $diretorio" >&2
  exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "Erro: python3 não está disponível." >&2
  exit 2
fi

tmp_dir=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp_dir"' EXIT

total=0
aprovados=0
reprovados=0

while IFS= read -r -d '' teste; do
  total=$((total + 1))
  gabarito="${teste%.c}.gabarito"
  saida="$tmp_dir/saida-$total.txt"

  if [[ ! -f "$gabarito" ]]; then
    printf '[FALHA] %s — gabarito não encontrado\n' "$teste"
    reprovados=$((reprovados + 1))
    continue
  fi

  python3 "$arquivo_fonte" "$teste" >"$saida" 2>&1 || true

  if diff -u -- "$gabarito" "$saida" >"$tmp_dir/diff-$total.txt"; then
    printf '[OK]     %s\n' "$teste"
    aprovados=$((aprovados + 1))
  else
    printf '[FALHA] %s\n' "$teste"
    cat "$tmp_dir/diff-$total.txt"
    reprovados=$((reprovados + 1))
  fi
done < <(find "$diretorio" -type f -name '*.c' -print0 | sort -z)

if ((total == 0)); then
  echo "Erro: nenhum arquivo .c encontrado em $diretorio" >&2
  exit 2
fi

printf '\nResultado: %d/%d aprovados; %d reprovados.\n' \
  "$aprovados" "$total" "$reprovados"
