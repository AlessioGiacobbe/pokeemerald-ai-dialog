#!/usr/bin/env bash
# Preview how the local model would rewrite a few NPC lines for a given
# style/language, using the same prompt shape the game uses — WITHOUT
# launching the game. Fast loop for tuning your style wording.
#
# Usage:
#   ./ai_preview.sh "STYLE" ["LANGUAGE"] ["MODEL"]
#
# Examples:
#   ./ai_preview.sh "the speaker is absolutely furious and shouting"
#   ./ai_preview.sh "talk like a pirate" "" qwen2.5:1.5b
#   ./ai_preview.sh "sempre molto arrabbiato" "Italian" qwen2.5:3b
set -euo pipefail

STYLE="${1:-}"
LANG_="${2:-}"
MODEL="${3:-qwen2.5:3b}"
URL="http://127.0.0.1:11434/v1/chat/completions"

TEMP="${TEMP:-0.9}"
SYS="You rewrite one line of NPC dialog for Pokemon Emerald. Reply with only the spoken line, at most 2 short sentences. No stage directions, no quotes. Keep the underlying facts/directions, but the STYLE is the top priority: fully commit to it, even rewriting drastically. Never break character."
[ -n "$LANG_" ] && SYS="$SYS Write the line in $LANG_, natural and idiomatic."
[ -n "$STYLE" ] && SYS="$SYS IMPORTANT STYLE - apply no matter what: $STYLE"

# A few representative NPC lines to preview against.
SAMPLES=(
  "an old woman|The trees here are lovely this time of year."
  "a hiker|The path north leads to the cave. Be careful in there!"
  "a youngster|Hi! I like shorts! They are comfy and easy to wear!"
  "a nurse|Welcome to the Pokemon Center. We can heal your Pokemon."
  "a fisherman|I have not caught anything all day. The sea is quiet."
)

echo "model=$MODEL  language=${LANG_:-English}  style=${STYLE:-<none>}"
echo "------------------------------------------------------------"
for s in "${SAMPLES[@]}"; do
  who="${s%%|*}"; line="${s#*|}"
  usr="Speaker: $who. Scripted line: \"$line\""$'\n'
  [ -n "$STYLE" ] && usr="${usr}Rewrite it fully in this style, no exceptions: $STYLE"$'\n'
  usr="${usr}Write the speaker's line."
  body=$(python3 -c '
import json,sys
print(json.dumps({"model":sys.argv[1],"max_tokens":200,"temperature":float(sys.argv[4]),
 "messages":[{"role":"system","content":sys.argv[2]},
             {"role":"user","content":sys.argv[3]}]}))' "$MODEL" "$SYS" "$usr" "$TEMP")
  out=$(curl -s "$URL" -H "content-type: application/json" -d "$body" \
        | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["choices"][0]["message"]["content"])' 2>/dev/null || echo "(error)")
  printf "%-14s %s\n" "$who:" "$out"
done
