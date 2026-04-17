#!/usr/bin/env bash
# Claude launcher — named accounts with per-account config dirs
#
# c --add|-a <name>      log in and store an account (browser OAuth)
# c --list|-l            list accounts
# c --whoami|-w          show active account + email
# c --account <name>     use a specific account (persists to .claude-account)
# c --temp|-t <name>     use account for this session only (no .claude-account written)
# c --statusline|-s      print statusline (path, branch, account)
# c --sync-mcp [name]    sync mcpServers for one account (or all if omitted)
# c --remove|-r <name>   remove an account
# c [account] [args]     resolve account if set, else use Claude's default

CONF_DIR="$HOME/.config/claude-accounts"
ACCOUNTS_FILE="$CONF_DIR/accounts"
CANONICAL_DIR="$HOME/.claude"

_account_dir() { echo "$CONF_DIR/$1"; }

# ---------------------------------------------------------------------------
# Git setup
# ---------------------------------------------------------------------------

_git_ensure_canonical() {
  [[ -d "$CANONICAL_DIR/.git" ]] && return
  git -C "$CANONICAL_DIR" init -q
  cat > "$CANONICAL_DIR/.gitignore" <<'EOF'
*
!.claude.json
!.gitignore
EOF
  git -C "$CANONICAL_DIR" add .claude.json .gitignore 2>/dev/null
  git -C "$CANONICAL_DIR" commit -q --allow-empty -m "init"
}

_git_ensure_profile() {
  local dir="$1"
  [[ -d "$dir/.git" ]] && return
  git -C "$dir" init -q
  cat > "$dir/.gitignore" <<'EOF'
*
!.claude.json
!.mcp-sync
!.gitignore
EOF
  git -C "$dir" add .claude.json .gitignore 2>/dev/null
  git -C "$dir" commit -q --allow-empty -m "init"
}

# ---------------------------------------------------------------------------
# MCP sync
# ---------------------------------------------------------------------------

_mcp_sync_account() {
  local account="$1"
  local adir
  adir=$(_account_dir "$account")
  [[ ! -f "$adir/.claude.json" ]] && return

  _git_ensure_canonical
  _git_ensure_profile "$adir"

  python3 - "$CANONICAL_DIR" "$adir" "$account" <<'PYEOF'
import json, os, subprocess, sys

canonical_dir = sys.argv[1]
profile_dir   = sys.argv[2]
account       = sys.argv[3]

canonical_json = os.path.join(canonical_dir, ".claude.json")
profile_json   = os.path.join(profile_dir,   ".claude.json")
mcp_sync_file  = os.path.join(profile_dir,   ".mcp-sync")

def load_mcp(path):
    try:
        with open(path) as f: return json.load(f).get("mcpServers", {})
    except: return {}

def load_json(path):
    try:
        with open(path) as f: return json.load(f)
    except: return {}

def save_json(path, d):
    with open(path, "w") as f: json.dump(d, f, indent=2)

def git_mcp_at_ref(repo, filepath, ref):
    rel = os.path.relpath(filepath, repo)
    r = subprocess.run(["git", "-C", repo, "show", f"{ref}:{rel}"],
                       capture_output=True, text=True)
    if r.returncode == 0:
        try: return json.loads(r.stdout).get("mcpServers", {})
        except: pass
    return {}

def git_head(repo):
    r = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"],
                       capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None

def git_commit(repo, files, message):
    subprocess.run(["git", "-C", repo, "add"] + files, capture_output=True)
    r = subprocess.run(["git", "-C", repo, "diff", "--cached", "--quiet"])
    if r.returncode != 0:
        subprocess.run(["git", "-C", repo, "commit", "-q", "-m", message])

def delta(before, after):
    added   = {k: v for k, v in after.items() if k not in before}
    changed = {k: v for k, v in after.items() if k in before and v != before[k]}
    removed = {k for k in before if k not in after}
    return added, changed, removed

def apply(target, added, changed, removed, skip=None):
    skip = skip or set()
    result = dict(target)
    for k, v in {**added, **changed}.items():
        if k not in skip: result[k] = v
    for k in removed:
        if k not in skip: result.pop(k, None)
    return result

# Current state
cur_canonical = load_mcp(canonical_json)
cur_profile   = load_mcp(profile_json)

# What canonical looked like at last sync (commit hash stored in profile)
last_hash = None
if os.path.exists(mcp_sync_file):
    last_hash = open(mcp_sync_file).read().strip() or None

head_canonical = git_mcp_at_ref(canonical_dir, canonical_json, last_hash) if last_hash else {}
head_profile   = git_mcp_at_ref(profile_dir,   profile_json,  "HEAD")

# What changed
can_added, can_changed, can_removed = delta(head_canonical, cur_canonical)
pro_added, pro_changed, pro_removed = delta(head_profile,   cur_profile)

# Profile wins on conflicts
profile_touched = set(pro_added) | set(pro_changed) | pro_removed

# Merge
new_profile   = apply(cur_profile,   can_added, can_changed, can_removed, skip=profile_touched)
new_canonical = apply(cur_canonical, pro_added, pro_changed, pro_removed)

# Write
def write_mcp(path, new_mcp):
    d = load_json(path)
    d["mcpServers"] = new_mcp
    save_json(path, d)

write_mcp(profile_json,   new_profile)
write_mcp(canonical_json, new_canonical)

# Commit canonical, then record its new HEAD in profile
git_commit(canonical_dir, [".claude.json"], f"sync: {account}")
new_canonical_head = git_head(canonical_dir)
if new_canonical_head:
    with open(mcp_sync_file, "w") as f: f.write(new_canonical_head + "\n")

# Commit profile
git_commit(profile_dir, [".claude.json", ".mcp-sync"], f"sync: {account}")

# Report
if can_added or can_changed or can_removed:
    print(f"  ← canonical: +{list(can_added)} ~{list(can_changed)} -{list(can_removed)}")
if pro_added or pro_changed or pro_removed:
    print(f"  → {account}: +{list(pro_added)} ~{list(pro_changed)} -{list(pro_removed)}")
PYEOF
}

_mcp_sync_all() {
  _git_ensure_canonical
  while IFS= read -r acc; do
    [[ -n "$acc" ]] && { echo "syncing $acc..."; _mcp_sync_account "$acc"; }
  done < "$ACCOUNTS_FILE" 2>/dev/null
}

# ---------------------------------------------------------------------------
# History
# ---------------------------------------------------------------------------

_mcp_history_update() {
  local servers_json="$1"
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

# ---------------------------------------------------------------------------
# Account helpers
# ---------------------------------------------------------------------------

_register() {
  mkdir -p "$CONF_DIR"
  grep -qx "$1" "$ACCOUNTS_FILE" 2>/dev/null || echo "$1" >> "$ACCOUNTS_FILE"
}

_resolve_account() {
  local account=""
  local dir="$PWD"
  while [[ "$dir" != "/" ]]; do
    [[ -f "$dir/.claude-account" ]] && { account=$(< "$dir/.claude-account"); break; }
    dir=$(dirname "$dir")
  done
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

# ---------------------------------------------------------------------------
# Statusline
# ---------------------------------------------------------------------------

_run_statusline() {
  local RED='\033[31m'
  local RESET='\033[0m'

  local P
  P=$(pwd | sed "s|$HOME|~|")
  [ ${#P} -gt 40 ] && P="...${P: -37}"

  local B
  B=$(git branch --show-current 2>/dev/null)
  [ -n "$B" ] && P="${P}[${B}]"

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

# ---------------------------------------------------------------------------
# Parse flags
# ---------------------------------------------------------------------------

account="" cmd="" passthrough=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --add|-a)        cmd=add;      account="$2"; shift 2 ;;
    --remove|-r)     cmd=remove;   account="$2"; shift 2 ;;
    --list|-l)       cmd=list;     shift ;;
    --whoami|-w)     cmd=whoami;   shift ;;
    --account)       account="$2"; shift 2 ;;
    --account=*)     account="${1#--account=}"; shift ;;
    --temp|-t)       cmd=temp;     account="$2"; shift 2 ;;
    --statusline|-s) _run_statusline; exit 0 ;;
    --sync-mcp)      cmd=sync-mcp; [[ "${2:-}" != -* && -n "${2:-}" ]] && { account="$2"; shift; }; shift ;;
    *)               passthrough+=("$1"); shift ;;
  esac
done

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

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
    _git_ensure_canonical
    _git_ensure_profile "$adir"
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
    _mcp_sync_account "$account"
    C_ACCOUNT="$account" C_TEMP=1 CLAUDE_CONFIG_DIR="$adir" claude "${passthrough[@]}"
    _mcp_sync_account "$account"
    ;;

  sync-mcp)
    if [[ -n "$account" ]]; then
      _mcp_sync_account "$account"
    else
      _mcp_sync_all
    fi
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
      _mcp_sync_account "$account"
      C_ACCOUNT="$account" CLAUDE_CONFIG_DIR="$adir" claude "${passthrough[@]}"
      _mcp_sync_account "$account"
    else
      claude "${passthrough[@]}"
    fi
    ;;
esac
