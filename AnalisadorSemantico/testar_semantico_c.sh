#!/usr/bin/env bash
# Testa o compilador MINIC em C (com o analisador semântico integrado)
# contra a suíte de 20 casos semânticos (testes-semantico-20), no mesmo
# formato do script oficial do professor.
#
# IMPORTANTE: o semantic.c é o COMPONENTE semântico (sem main). Para
# usar este script, o arquivo passado deve conter o pipeline completo
# (scanner + parser + semântico + main), compilável em um único comando:
#
#   gcc -Wall -Wextra <pipeline.c> -o <executavel>
#
# O executável deve aceitar:  ./compilador arquivo.c
# e imprimir a saída canônica comparável byte a byte com o gabarito.
#
# Uso:
#   ./testar_semantico_c.sh [pipeline.c] [pasta-de-testes]
# Exemplo:
#   ./testar_semantico_c.sh ./compilador.c ./testes-semantico-20

set -u

arquivo_fonte=${1:-./compilador.c}
diretorio=${2:-./testes-semantico-20}

if [[ ! -f "$arquivo_fonte" ]]; then
  echo "Erro: fonte do analisador não encontrado: $arquivo_fonte" >&2
  exit 2
fi
if [[ ! -d "$diretorio" ]]; then
  echo "Erro: diretório de testes não encontrado: $diretorio" >&2
  exit 2
fi
if ! command -v gcc >/dev/null 2>&1; then
  echo "Erro: gcc não está disponível." >&2
  exit 2
fi

tmp_dir=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp_dir"' EXIT

executavel="$tmp_dir/analisador"
if ! gcc -Wall -Wextra "$arquivo_fonte" -o "$executavel"; then
  echo "Erro: a compilação do analisador falhou." >&2
  exit 2
fi

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

  "$executavel" "$teste" >"$saida" 2>&1 || true

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
