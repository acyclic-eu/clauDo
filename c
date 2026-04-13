#!/usr/bin/env bash
# Claude launcher — named accounts with API keys in macOS Keychain
#
# c --add <name>      store an API key for an account
# c --list            list accounts (* = default)
# c --default <name>  set default account
# c --whoami          show active account + email
# c --account <name>  use a specific account (persists to .claude-account)
# c [args]            resolve account, inject key, launch claude

CONF_DIR="$HOME/.config/claude-accounts"
ACCOUNTS_FILE="$CONF_DIR/accounts"
DEFAULT_FILE="$CONF_DIR/default"
KEYCHAIN_SERVICE="claude-code"

_keychain_set()  { security add-generic-password -U -a "$1" -s "$KEYCHAIN_SERVICE" -w "$2"; }
_keychain_get()  { security find-generic-password -a "$1" -s "$KEYCHAIN_SERVICE" -w 2>/dev/null; }
_keychain_del()  { security delete-generic-password -a "$1" -s "$KEYCHAIN_SERVICE" 2>/dev/null; }

_register() {
  mkdir -p "$CONF_DIR"
  grep -qx "$1" "$ACCOUNTS_FILE" 2>/dev/null || echo "$1" >> "$ACCOUNTS_FILE"
  [[ -f "$DEFAULT_FILE" ]] || echo "$1" > "$DEFAULT_FILE"
}

_resolve_account() {
  local account=""
  # 1. Walk up for .claude-account
  if [[ -z "$account" ]]; then
    local dir="$PWD"
    while [[ "$dir" != "/" ]]; do
      [[ -f "$dir/.claude-account" ]] && { account=$(< "$dir/.claude-account"); break; }
      dir=$(dirname "$dir")
    done
  fi
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
    --add)      cmd=add;     account="$2"; shift 2 ;;
    --remove)   cmd=remove;  account="$2"; shift 2 ;;
    --default)  cmd=default; account="$2"; shift 2 ;;
    --list)     cmd=list;    shift ;;
    --whoami)   cmd=whoami;  shift ;;
    --account)  account="$2"; shift 2 ;;
    --account=*) account="${1#--account=}"; shift ;;
    *)          passthrough+=("$1"); shift ;;
  esac
done

case "$cmd" in
  add)
    [[ -z "$account" ]] && { echo "Usage: c --add <name>"; exit 1; }
    read -rsp "API key for '$account': " key; echo
    _keychain_set "$account" "$key"
    _register "$account"
    echo "Account '$account' stored in Keychain."
    ;;
  remove)
    _keychain_del "$account"
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
    key=$(_keychain_get "$account")
    [[ -z "$key" ]] && { echo "No key for '$account'"; exit 1; }
    echo -n "[$account] "
    ANTHROPIC_API_KEY="$key" claude auth status \
      | python3 -c "import sys,json; print(json.load(sys.stdin)['email'])"
    ;;
  *)
    [[ -n "$account" ]] && echo "$account" > "$PWD/.claude-account"
    account=$(_resolve_account)
    key=$(_keychain_get "$account")
    [[ -z "$key" ]] && { echo "No key for '$account'. Run: c --add $account"; exit 1; }
    ANTHROPIC_API_KEY="$key" exec claude "${passthrough[@]}"
    ;;
esac
