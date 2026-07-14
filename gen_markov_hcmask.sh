#!/usr/bin/env bash
#
# gen_markov_hcmask.sh - drive crackalack_gen over a hashcat .hcmask file with
# Markov ordering enabled.
#
# The native --hcmask batch mode is mutually exclusive with --markov, so this
# wrapper does what the binary won't: it parses the .hcmask file itself (matching
# hcmask.c line rules), derives each mask's length (matching mask_parse.c position
# counting), and runs one `crackalack_gen ... --mask <mask> --markov <model>`
# invocation per active line with min_len == max_len == that mask's length.
#
# Usage:
#   ./gen_markov_hcmask.sh <hash> <charset> <table_index> <chain_len> \
#       <num_chains> <part_index> --hcmask FILE --markov MODEL \
#       [--markov-keyspace K] [-gws GWS] [-1 CS] [-2 CS] [-3 CS] [-4 CS]
#
# Notes:
#   * min_len / max_len are intentionally NOT positional args here: each table's
#     length is derived from its own mask (as native --hcmask does).
#   * Global -1..-4 act as defaults; an inline charset on a .hcmask line wins.
#   * <charset> is passed through but ignored by gen under --mask (kept for
#     positional parity with crackalack_gen).

set -euo pipefail

GEN="${GEN:-./crackalack_gen}"

die() { echo "error: $*" >&2; exit 1; }

# --- positional args ---------------------------------------------------------
[ $# -ge 6 ] || die "need <hash> <charset> <table_index> <chain_len> <num_chains> <part_index> ... (see header)"
HASH=$1; CHARSET=$2; TIDX=$3; CHAIN=$4; NCHAINS=$5; PART=$6
shift 6

# --- flags -------------------------------------------------------------------
HCMASK=""; MODEL=""; KEYSPACE=""; GWS=""
declare -A GCC=()   # global custom charsets, keyed 1..4
while [ $# -gt 0 ]; do
  case "$1" in
    --hcmask)          HCMASK=${2:?--hcmask needs a file}; shift 2;;
    --markov)          MODEL=${2:?--markov needs a model};  shift 2;;
    --markov-keyspace) KEYSPACE=${2:?--markov-keyspace needs N}; shift 2;;
    -gws)              GWS=${2:?-gws needs a value};         shift 2;;
    -1|-2|-3|-4)       GCC[${1#-}]=${2:?$1 needs chars};     shift 2;;
    *) die "unknown arg: $1";;
  esac
done

[ -n "$HCMASK" ] || die "--hcmask FILE is required"
[ -n "$MODEL" ]  || die "--markov MODEL is required"
[ -f "$HCMASK" ] || die "no such .hcmask file: $HCMASK"
[ -f "$MODEL" ]  || die "no such markov model: $MODEL"
[ -x "$GEN" ] || command -v "$GEN" >/dev/null 2>&1 || die "crackalack_gen not found (set GEN=)"

# --- parse the .hcmask file (mirrors hcmask.c:hcmask_parse_line) -------------
# Emit one record per active line, fields separated by US (0x1f):
#   <ncc><US><cc1>...<US><ccN><US><mask>
# Split on UNESCAPED commas; unescape \, -> , and \# -> #; skip blank/# lines.
US=$'\037'
parse_awk='
BEGIN { US = sprintf("%c", 31) }   # unit separator, matches bash $US (0x1f)
{
  line=$0
  # strip trailing CR (in case of CRLF files)
  sub(/\r$/, "", line)
  # blank / comment? (first non-space char)
  s=line; sub(/^[ \t]+/, "", s)
  if (s=="" || substr(s,1,1)=="#") next

  n=length(line); i=1; f=""; nf=0; delete fields
  while (i<=n) {
    c=substr(line,i,1)
    if (c=="\\" && i<n) {
      d=substr(line,i+1,1)
      if (d=="," ) { f=f ","; i+=2; continue }
      if (d=="#" ) { f=f "#"; i+=2; continue }
      f=f c; i++; continue          # other backslashes pass through verbatim
    }
    if (c==",") { fields[++nf]=f; f=""; i++; continue }
    f=f c; i++
  }
  fields[++nf]=f                     # final field

  if (nf>5) { printf("SKIP\tline has >5 fields: %s\n", line) > "/dev/stderr"; next }
  mask=fields[nf]
  if (mask=="") { printf("SKIP\tempty mask: %s\n", line) > "/dev/stderr"; next }
  ncc=nf-1
  out=ncc
  for (k=1;k<=ncc;k++) out=out US fields[k]
  out=out US mask
  print out
}
'

# --- mask length: count positions, not chars (mirrors mask_parse.c) ----------
mask_len() {
  local m=$1 i=0 n=${#1} pos=0 c
  while [ $i -lt $n ]; do
    c=${m:$i:1}
    if [ "$c" = "?" ] && [ $((i+1)) -lt $n ]; then
      pos=$((pos+1)); i=$((i+2))
    else
      pos=$((pos+1)); i=$((i+1))
    fi
  done
  echo "$pos"
}

# --- drive gen ---------------------------------------------------------------
count=0; ok=0
while IFS= read -r rec; do
  count=$((count+1))
  # split record on US
  IFS="$US" read -r ncc rest <<<"$rec"
  IFS="$US" read -ra parts <<<"$rest"
  # parts[] = cc1..ccN, mask   (ncc charsets then the mask)
  mask=${parts[$ncc]}
  len=$(mask_len "$mask")

  # per-slot charset: inline (from this line) overrides the global default
  declare -A cc=()
  for k in 1 2 3 4; do [ -n "${GCC[$k]:-}" ] && cc[$k]=${GCC[$k]}; done
  for ((k=1; k<=ncc; k++)); do cc[$k]=${parts[$((k-1))]}; done

  # assemble argv
  args=("$HASH" "$CHARSET" "$len" "$len" "$TIDX" "$CHAIN" "$NCHAINS" "$PART"
        --mask "$mask" --markov "$MODEL")
  [ -n "$KEYSPACE" ] && args+=(--markov-keyspace "$KEYSPACE")
  for k in 1 2 3 4; do [ -n "${cc[$k]:-}" ] && args+=("-$k" "${cc[$k]}"); done
  [ -n "$GWS" ] && args+=(-gws "$GWS")

  echo "=== [$count] mask '$mask' (len $len) ==="
  if "$GEN" "${args[@]}"; then
    ok=$((ok+1))
  else
    die "gen failed on mask '$mask' (record $count)"
  fi
done < <(awk "$parse_awk" "$HCMASK")

[ "$count" -gt 0 ] || die "no active masks in $HCMASK"
echo "done: generated $ok/$count masked+markov tables."
