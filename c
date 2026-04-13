#!/usr/bin/env bash
# Claude launcher — named accounts with per-account config dirs
#
# c --add <name>      log in and store an account (browser OAuth)
# c --list            list accounts (* = default)
# c --default <name>  set default account
# c --whoami          show active account + email
# c --account <name>  use a specific account (persists to .claude-account)
# c [args]            resolve account, set config dir, launch claude

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

# --- Parse flags ---
account="" cmd="" passthrough=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --add)       cmd=add;     account="$2"; shift 2 ;;
    --remove)    cmd=remove;  account="$2"; shift 2 ;;
    --default)   cmd=default; account="$2"; shift 2 ;;
    --list)      cmd=list;    shift ;;
    --whoami)    cmd=whoami;  shift ;;
    --account)   account="$2"; shift 2 ;;
    --account=*) account="${1#--account=}"; shift ;;
    *)           passthrough+=("$1"); shift ;;
  esac
done

case "$cmd" in
  add)
    [[ -z "$account" ]] && { echo "Usage: c --add <name>"; exit 1; }
    adir=$(_account_dir "$account")
    mkdir -p "$adir"
    ln -sf "$HOME/.claude/settings.json" "$adir/settings.json"
    ln -sf "$HOME/.claude/CLAUDE.md" "$adir/CLAUDE.md"
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
  *)
    [[ -n "$account" ]] && echo "$account" > "$PWD/.claude-account"
    account=$(_resolve_account)
    adir=$(_account_dir "$account")
    [[ ! -d "$adir" ]] && { echo "No config for '$account'. Run: c --add $account"; exit 1; }
    CLAUDE_CONFIG_DIR="$adir" exec claude "${passthrough[@]}"
    ;;
esac
