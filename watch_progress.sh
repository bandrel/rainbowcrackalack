#!/bin/bash
FILE="${1:-ntlm_ascii-32-95#8-8_0_422000x158397608_0.rt}"
TOTAL=158397608
while true; do
  SIZE=$(stat -f %z "$FILE" 2>/dev/null || echo 0)
  awk -v s="$SIZE" -v t="$TOTAL" 'BEGIN {
    printf "%.1f%% (%d / %d chains, %.0f MB / %.0f MB)\n",
      s/16/t*100, s/16, t, s/1048576, t*16/1048576
  }'
  sleep 2
done
