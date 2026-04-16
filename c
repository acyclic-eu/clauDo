#!/usr/bin/env bash
# Claude launcher — named accounts with per-account config dirs
#
# c --add|-a <name>      log in and store an account (browser OAuth)
# c --list|-l            list accounts
# c --whoami|-w          show active account + email
# c --account <name>     use a specific account (persists to .claude-account)
# c --temp|-t <name>     use account for this session only (no .claude-account written)
# c --statusline|-s      print statusline (path, branch, account)
# c --remove|-r <name>   remove an account
# c [account] [args]     resolve account if set, else use Claude's default

CONF_DIR="$HOME/.config/claude-accounts"
ACCOUNTS_FILE="$CONF_DIR/accounts"

_account_dir() { echo "$CONF_DIR/$1"; }

# Sync mcpServers bidirectionally between ~/.claude/.claude.json (canonical) and all profile .claude.json files.
# Any profile can add servers via /mcp add; they get merged into the canonical on next launch.
# Record mcpServers into ~/.claude/mcp-history.json (never removes entries).
_mcp_history_update() {
  local servers_json="$1"   # JSON string of mcpServers dict
  local history="$HOME/.claude/mcp-history.json"
  python3 - "$history" "$servers_json" <<'PYEOF'
import json, sys
from datetime import datetime, timezone

history_path = sys.argv[1]
servers      = json.loads(sys.argv[2])

try:
  with open(history_path) as f:
    history = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
  history = {}

now = datetime.now(timezone.utc).isoformat()
for name, config in servers.items():
  if name in history:
    history[name]["lastSeen"] = now
    history[name]["config"]   = config
  else:
    history[name] = {"config": config, "firstSeen": now, "lastSeen": now}

with open(history_path, "w") as f:
  json.dump(history, f, indent=2)
PYEOF
}

# Push canonical mcpServers (~/.claude/.claude.json) to all profiles.
_sync_mcp_push() {
  local default_json="$HOME/.claude/.claude.json"
  [[ ! -f "$default_json" ]] && return
  python3 - "$default_json" "$CONF_DIR" <<'PYEOF'
import json, os, sys

default_path = sys.argv[1]
conf_dir     = sys.argv[2]

def load(p):
  with open(p) as f: return json.load(f)
def save(p, d):
  with open(p, "w") as f: json.dump(d, f, indent=2)

canonical = load(default_path).get("mcpServers", {})

accounts_file = os.path.join(conf_dir, "accounts")
if not os.path.exists(accounts_file):
  sys.exit(0)
with open(accounts_file) as f:
  accounts = [l.strip() for l in f if l.strip()]

for acc in accounts:
  p = os.path.join(conf_dir, acc, ".claude.json")
  if not os.path.exists(p): continue
  d = load(p)
  if d.get("mcpServers") != canonical:
    d["mcpServers"] = canonical
    save(p, d)
PYEOF
  # Record current canonical in history
  local canonical
  canonical=$(python3 -c "import json; d=json.load(open('$HOME/.claude/.claude.json')); print(json.dumps(d.get('mcpServers',{})))" 2>/dev/null)
  [[ -n "$canonical" ]] && _mcp_history_update "$canonical"
}

# After a session ends, diff the active profile's mcpServers against the before-snapshot.
# Additions and removals are applied to canonical and all other profiles.
_sync_mcp_diff() {
  local profile_json="$1"
  local snapshot="$2"
  local default_json="$HOME/.claude/.claude.json"
  [[ ! -f "$profile_json" || ! -f "$snapshot" || ! -f "$default_json" ]] && return
  python3 - "$profile_json" "$snapshot" "$default_json" "$CONF_DIR" <<'PYEOF'
import json, os, sys

profile_path  = sys.argv[1]
snapshot_path = sys.argv[2]
default_path  = sys.argv[3]
conf_dir      = sys.argv[4]

def load(p):
  with open(p) as f: return json.load(f)
def save(p, d):
  with open(p, "w") as f: json.dump(d, f, indent=2)

before   = json.loads(open(snapshot_path).read())
after    = load(profile_path).get("mcpServers", {})
added    = {k: v for k, v in after.items()   if k not in before}
removed  = {k     for k    in before.keys()  if k not in after}

if not added and not removed:
  sys.exit(0)

accounts_file = os.path.join(conf_dir, "accounts")
with open(accounts_file) as f:
  accounts = [l.strip() for l in f if l.strip()]

all_paths = [default_path] + [
  os.path.join(conf_dir, acc, ".claude.json")
  for acc in accounts
  if os.path.exists(os.path.join(conf_dir, acc, ".claude.json"))
]

for p in all_paths:
  if p == profile_path:
    continue
  d = load(p)
  mcp = dict(d.get("mcpServers", {}))
  for k, v in added.items():
    mcp[k] = v
  for k in removed:
    mcp.pop(k, None)
  d["mcpServers"] = mcp
  save(p, d)

# Also update canonical
d = load(default_path)
mcp = dict(d.get("mcpServers", {}))
for k, v in added.items():
  mcp[k] = v
for k in removed:
  mcp.pop(k, None)
d["mcpServers"] = mcp
save(default_path, d)

if added:   print(f"MCP sync: added {list(added.keys())}")
if removed: print(f"MCP sync: removed {list(removed)}")
# Output added servers as JSON for history update
print(f"__added_json__:{json.dumps(added)}")
PYEOF
  # Update history with the post-session state (history never removes entries)
  local after_json
  after_json=$(python3 -c "import json; d=json.load(open('$profile_json')); print(json.dumps(d.get('mcpServers',{})))" 2>/dev/null)
  [[ -n "$after_json" ]] && _mcp_history_update "$after_json"
  rm -f "$snapshot"
}

_register() {
  mkdir -p "$CONF_DIR"
  grep -qx "$1" "$ACCOUNTS_FILE" 2>/dev/null || echo "$1" >> "$ACCOUNTS_FILE"
}

_resolve_account() {
  local account=""
  # 1. Walk up for .claude-account
  local dir="$PWD"
  while [[ "$dir" != "/" ]]; do
    [[ -f "$dir/.claude-account" ]] && { account=$(< "$dir/.claude-account"); break; }
    dir=$(dirname "$dir")
  done
  # 2. Prompt if multiple accounts and none resolved
  if [[ -z "$account" ]]; then
    mapfile -t accounts < "$ACCOUNTS_FILE" 2>/dev/null
    (( ${#accounts[@]} == 1 )) && account="${accounts[0]}"
    if [[ -z "$account" && ${#accounts[@]} -gt 1 ]]; then
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
    --add|-a)        cmd=add;     account="$2"; shift 2 ;;
    --remove|-r)     cmd=remove;  account="$2"; shift 2 ;;
    --list|-l)       cmd=list;    shift ;;
    --whoami|-w)     cmd=whoami;  shift ;;
    --account)       account="$2"; shift 2 ;;
    --account=*)     account="${1#--account=}"; shift ;;
    --temp|-t)       cmd=temp;    account="$2"; shift 2 ;;
    --statusline|-s) _run_statusline; exit 0 ;;
    *)               passthrough+=("$1"); shift ;;
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
    [[ ! -e "$adir/projects" ]] && ln -s "$HOME/.claude/projects" "$adir/projects"
    [[ ! -e "$adir/sessions" ]] && ln -s "$HOME/.claude/sessions" "$adir/sessions"
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
  list)
    cat "$ACCOUNTS_FILE" 2>/dev/null
    ;;
  whoami)
    account=$(_resolve_account)
    [[ -z "$account" ]] && { echo "No account active (Claude default)"; exit 0; }
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
    _sync_mcp_push
    snapshot=$(mktemp)
    python3 -c "import json; d=json.load(open('$adir/.claude.json')); print(json.dumps(d.get('mcpServers',{})))" > "$snapshot" 2>/dev/null
    C_ACCOUNT="$account" C_TEMP=1 CLAUDE_CONFIG_DIR="$adir" claude "${passthrough[@]}"
    _sync_mcp_diff "$adir/.claude.json" "$snapshot"
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
    if [[ -n "$account" ]]; then
      adir=$(_account_dir "$account")
      [[ ! -d "$adir" ]] && { echo "No config for '$account'. Run: c --add $account"; exit 1; }
      _sync_mcp_push
      snapshot=$(mktemp)
      python3 -c "import json; d=json.load(open('$adir/.claude.json')); print(json.dumps(d.get('mcpServers',{})))" > "$snapshot" 2>/dev/null
      C_ACCOUNT="$account" CLAUDE_CONFIG_DIR="$adir" claude "${passthrough[@]}"
      _sync_mcp_diff "$adir/.claude.json" "$snapshot"
    else
      _sync_mcp_push
      snapshot=$(mktemp)
      python3 -c "import json; d=json.load(open('$HOME/.claude/.claude.json')); print(json.dumps(d.get('mcpServers',{})))" > "$snapshot" 2>/dev/null
      claude "${passthrough[@]}"
      _sync_mcp_diff "$HOME/.claude/.claude.json" "$snapshot"
    fi
    ;;
esac
