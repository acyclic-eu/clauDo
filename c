#!/usr/bin/env bash
# Claude Code launcher — multi-account with per-project config
#
# Account resolution order:
#   1. --account <name> flag
#   2. .claude-account file (walks up from current dir)
#   3. ~/.config/claude-accounts/default
#   4. Interactive prompt (fzf if available, else select)
#
# To set a per-project account:
#   echo personal > .claude-account

CONF_DIR="$HOME/.config/claude-accounts"
ACCOUNTS_FILE="$CONF_DIR/accounts"
DEFAULT_FILE="$CONF_DIR/default"
SHARED_DIR="$HOME/.claude"

# Items to symlink from SHARED_DIR into new account dirs
SHARED_ITEMS=(settings.json CLAUDE.md commands plugins)

# --- Parse flags ---

account=""
whoami_mode=false
add_mode=false
list_mode=false
no_symlink=false
passthrough_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --account)
      account="$2"
      shift 2
      ;;
    --account=*)
      account="${1#--account=}"
      shift
      ;;
    --add)
      add_mode=true
      account="$2"
      shift 2
      ;;
    --list)
      list_mode=true
      shift
      ;;
    --whoami)
      whoami_mode=true
      shift
      ;;
    --no-symlink)
      no_symlink=true
      shift
      ;;
    *)
      passthrough_args+=("$1")
      shift
      ;;
  esac
done

# --- List accounts ---

if [[ "$list_mode" == true ]]; then
  default=$(cat "$DEFAULT_FILE" 2>/dev/null)
  while IFS= read -r acc; do
    if [[ "$acc" == "$default" ]]; then
      echo "* $acc (default)"
    else
      echo "  $acc"
    fi
  done < "$ACCOUNTS_FILE"
  exit 0
fi

# --- Helpers ---

_setup_account_dir() {
  local name="$1"
  local dir="$HOME/.claude-$name"
  mkdir -p "$dir"
  for item in "${SHARED_ITEMS[@]}"; do
    local src="$SHARED_DIR/$item"
    [[ -e "$src" ]] || continue
    [[ -e "$dir/$item" ]] && continue  # don't clobber existing
    ln -s "$src" "$dir/$item"
  done
}

_register_account() {
  local name="$1"
  if ! grep -qx "$name" "$ACCOUNTS_FILE" 2>/dev/null; then
    echo "$name" >> "$ACCOUNTS_FILE"
  fi
}

# --- Handle --add ---

if [[ "$add_mode" == true ]]; then
  [[ -z "$account" ]] && { echo "Usage: c --add <name>"; exit 1; }
  _setup_account_dir "$account"
  _register_account "$account"
  echo "Account '$account' ready at $HOME/.claude-$account"
  exit 0
fi

# --- Resolve account ---

# 1. Walk up directory tree for .claude-account
if [[ -z "$account" ]]; then
  dir="$PWD"
  while [[ "$dir" != "/" ]]; do
    if [[ -f "$dir/.claude-account" ]]; then
      account=$(cat "$dir/.claude-account")
      break
    fi
    dir=$(dirname "$dir")
  done
fi

# 2. Global default
if [[ -z "$account" ]] && [[ -f "$DEFAULT_FILE" ]]; then
  account=$(cat "$DEFAULT_FILE")
fi

# 3. Prompt
if [[ -z "$account" ]]; then
  if [[ ! -f "$ACCOUNTS_FILE" ]]; then
    echo "No accounts configured. Run: c --add <name>"
    exit 1
  fi

  mapfile -t accounts < "$ACCOUNTS_FILE"

  if [[ ${#accounts[@]} -eq 0 ]]; then
    echo "No accounts in $ACCOUNTS_FILE"
    exit 1
  elif [[ ${#accounts[@]} -eq 1 ]]; then
    account="${accounts[0]}"
  elif command -v fzf &>/dev/null; then
    account=$(printf '%s\n' "${accounts[@]}" | fzf --prompt="Claude account: " --height=~10)
  else
    echo "Select Claude account:"
    select acc in "${accounts[@]}"; do
      [[ -n "$acc" ]] && account="$acc" && break
    done
  fi
fi

[[ -z "$account" ]] && exit 1

# --- Setup dir and register ---

if [[ "$no_symlink" == false ]]; then
  _setup_account_dir "$account"
fi
_register_account "$account"

# --- whoami ---

if [[ "$whoami_mode" == true ]]; then
  # If running inside a Claude session, CLAUDE_CONFIG_DIR is already set — use it directly
  config_dir="${CLAUDE_CONFIG_DIR:-$HOME/.claude-$account}"
  CLAUDE_CONFIG_DIR="$config_dir" claude auth status \
    | python3 -c "import sys,json; e=json.load(sys.stdin)['email']; print('@' + e.split('@')[1].split('.')[0])"
  exit $?
fi

CLAUDE_CONFIG_DIR="$HOME/.claude-$account" claude "${passthrough_args[@]}"
