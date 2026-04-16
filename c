#!/usr/bin/env bash
# Claude launcher — named accounts with per-account config dirs
#
# c --add|-a <name>      log in and store an account (browser OAuth)
# c --list|-l            list accounts (* = default)
# c --default|-d <name>  set default account
# c --whoami|-w          show active account + email
# c --account <name>     use a specific account (persists to .claude-account)
# c --temp|-t <name>     use account for this session only (no .claude-account written)
# c --statusline|-s      configure ~/.claude/settings.json to use built-in statusline
# c --remove|-r <name>   remove an account
# c [account] [args]     resolve account implicitly, set config dir, launch claude

CONF_DIR="$HOME/.config/claude-accounts"
ACCOUNTS_FILE="$CONF_DIR/accounts"
DEFAULT_FILE="$CONF_DIR/default"

_account_dir() { echo "$CONF_DIR/$1"; }

_register() {
  mkdir -p "$CONF_DIR"
  grep -qx "$1" "$ACCOUNTS_FILE" 2>/dev/null || echo "$1" >> "$ACCOUNTS_FILE"
  [[ -f "$DEFAULT_FILE" ]] || echo "$1" > "$DEFAULT_FILE"
}

_resolve_account() {
  local account=""
  # 1. Walk up for .claude-account
  local dir="$PWD"
  while [[ "$dir" != "/" ]]; do
    [[ -f "$dir/.claude-account" ]] && { account=$(< "$dir/.claude-account"); break; }
    dir=$(dirname "$dir")
  done
  # 2. Global default
  [[ -z "$account" && -f "$DEFAULT_FILE" ]] && account=$(< "$DEFAULT_FILE")
  # 3. Prompt
  if [[ -z "$account" ]]; then
    mapfile -t accounts < "$ACCOUNTS_FILE" 2>/dev/null
    (( ${#accounts[@]} == 0 )) && { echo "No accounts. Run: c --add <name>"; exit 1; }
    (( ${#accounts[@]} == 1 )) && account="${accounts[0]}"
    if [[ -z "$account" ]]; then
      if command -v fzf &>/dev/null; then
        account=$(printf '%s\n' "${accounts[@]}" | fzf --prompt="Claude account: " --height=~10)
      else
        select acc in "${accounts[@]}"; do [[ -n "$acc" ]] && account="$acc" && break; done
      fi
    fi
  fi
  echo "$account"
}

_run_statusline() {
  local BOLD='\033[1m'
  local RED='\033[31m'
  local RESET='\033[0m'

  # Path (truncated)
  local P
  P=$(pwd | sed "s|$HOME|~|")
  [ ${#P} -gt 40 ] && P="...${P: -37}"

  # Git branch
  local B
  B=$(git branch --show-current 2>/dev/null)
  [ -n "$B" ] && P="${P}[${B}]"

  # Resolve account
  local ACC=""
  if [ -n "$C_ACCOUNT" ]; then
    ACC="$C_ACCOUNT"
  else
    local dir="$PWD"
    while [ "$dir" != "/" ]; do
      [ -f "$dir/.claude-account" ] && { ACC=$(cat "$dir/.claude-account"); break; }
      dir=$(dirname "$dir")
    done
    [ -z "$ACC" ] && ACC=$(cat "$HOME/.config/claude-accounts/default" 2>/dev/null)
  fi

  if [ -n "$ACC" ]; then
    local ACC_CDIR="$HOME/.config/claude-accounts/$ACC"
    local REAL_CDIR="${CLAUDE_CONFIG_DIR:-$HOME}"

    _get_dom() {
      python3 -c "
import json, sys
try:
  d = json.load(open('$1/.claude.json'))
  e = d.get('oauthAccount',{}).get('emailAddress','')
  dom = e.split('@')[1].split('.')[0] if '@' in e else ''
  print(dom)
except: sys.exit(1)
" 2>/dev/null
    }

    local ACC_DOM REAL_DOM
    ACC_DOM=$(_get_dom "$ACC_CDIR")
    REAL_DOM=$(_get_dom "$REAL_CDIR")

    [ "${C_TEMP:-0}" = "1" ] && local SEP="~/" || local SEP="/"

    if [ -n "$REAL_DOM" ] && [ "$ACC_DOM" != "$REAL_DOM" ]; then
      echo -e "$P (${ACC}${SEP}${RED}@${REAL_DOM}${RESET})"
    else
      local DOM="${ACC_DOM:-${REAL_DOM}}"
      echo "$P (${ACC}${SEP}@${DOM})"
    fi
  else
    echo "$P"
  fi
}

# --- Parse flags ---
account="" cmd="" passthrough=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --add|-a)     cmd=add;        account="$2"; shift 2 ;;
    --remove|-r)  cmd=remove;     account="$2"; shift 2 ;;
    --default|-d) cmd=default;    account="$2"; shift 2 ;;
    --list|-l)    cmd=list;       shift ;;
    --whoami|-w)  cmd=whoami;     shift ;;
    --account)    account="$2";   shift 2 ;;
    --account=*)  account="${1#--account=}"; shift ;;
    --temp|-t)    cmd=temp;       account="$2"; shift 2 ;;
    --statusline|-s) _run_statusline; exit 0 ;;
    *)            passthrough+=("$1"); shift ;;
  esac
done

case "$cmd" in
  add)
    [[ -z "$account" ]] && { echo "Usage: c --add <name>"; exit 1; }
    adir=$(_account_dir "$account")
    mkdir -p "$adir"
    ln -sf "$HOME/.claude/settings.json" "$adir/settings.json"
    if [[ -d "$adir/skills" && ! -L "$adir/skills" ]]; then
      mv "$adir/skills/"* "$HOME/.claude/skills/" 2>/dev/null
      rm -rf "$adir/skills"
    fi
    ln -s "$HOME/.claude/skills" "$adir/skills"
    if [[ -d "$adir/plugins" && ! -L "$adir/plugins" ]]; then
      mv "$adir/plugins/"* "$HOME/.claude/plugins/" 2>/dev/null
      rm -rf "$adir/plugins"
    fi
    ln -s "$HOME/.claude/plugins" "$adir/plugins"
    ln -sf "$HOME/.claude/CLAUDE.md" "$adir/CLAUDE.md"
    CLAUDE_CONFIG_DIR="$adir" claude auth login
    _register "$account"
    echo "Account '$account' ready."
    ;;
  remove)
    adir=$(_account_dir "$account")
    rm -rf "$adir"
    sed -i '' "/^${account}$/d" "$ACCOUNTS_FILE" 2>/dev/null
    echo "Account '$account' removed."
    ;;
  default)
    echo "$account" > "$DEFAULT_FILE"
    echo "Default set to '$account'."
    ;;
  list)
    default=$(< "$DEFAULT_FILE" 2>/dev/null)
    while IFS= read -r acc; do
      [[ "$acc" == "$default" ]] && echo "* $acc" || echo "  $acc"
    done < "$ACCOUNTS_FILE"
    ;;
  whoami)
    account=$(_resolve_account)
    adir=$(_account_dir "$account")
    email=$(CLAUDE_CONFIG_DIR="$adir" claude auth status 2>/dev/null \
      | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('email','unknown'))" 2>/dev/null)
    echo "[$account] $email"
    ;;
  temp)
    [[ -z "$account" ]] && { echo "Usage: c --temp <name>"; exit 1; }
    adir=$(_account_dir "$account")
    if [[ ! -d "$adir" ]]; then
      adir="$CONF_DIR/.tmp-$account"
      mkdir -p "$adir"
      ln -sf "$HOME/.claude/settings.json" "$adir/settings.json"
      [[ ! -e "$adir/skills" ]] && ln -s "$HOME/.claude/skills" "$adir/skills"
      [[ ! -e "$adir/plugins" ]] && ln -s "$HOME/.claude/plugins" "$adir/plugins"
      ln -sf "$HOME/.claude/CLAUDE.md" "$adir/CLAUDE.md"
    fi
    C_ACCOUNT="$account" C_TEMP=1 CLAUDE_CONFIG_DIR="$adir" exec claude "${passthrough[@]}"
    ;;
  *)
    # Implicit account: c work  →  c --account work
    if [[ -z "$account" && ${#passthrough[@]} -gt 0 ]]; then
      first="${passthrough[0]}"
      grep -qx "$first" "$ACCOUNTS_FILE" 2>/dev/null && {
        account="$first"; passthrough=("${passthrough[@]:1}")
      }
    fi
    [[ -n "$account" ]] && echo "$account" > "$PWD/.claude-account"
    account=$(_resolve_account)
    adir=$(_account_dir "$account")
    [[ ! -d "$adir" ]] && { echo "No config for '$account'. Run: c --add $account"; exit 1; }
    C_ACCOUNT="$account" CLAUDE_CONFIG_DIR="$adir" exec claude "${passthrough[@]}"
    ;;
esac
