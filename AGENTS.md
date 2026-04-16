# AGENTS.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Repository scope
- This repo is a single Bash CLI launcher (`c`) for running Claude Code with per-account configuration directories.
- Core behavior and usage are defined in `c` and `README.md`.

## Primary commands
## Install / local setup
```sh
cp c ~/.local/bin/c
chmod +x ~/.local/bin/c
```

## Runtime commands (core workflow)
```sh
c
c --account <name>
c --add <name>
c --remove <name>
c --default <name>
c --list
c --whoami
echo <name> > .claude-account
```

## Development checks in this repo
- There is no formal build system or test suite configured in the repository.
- Use syntax/static checks directly on the script:
```sh
bash -n ./c
shellcheck ./c
```
- For focused behavior checks (“single test” equivalent), run the specific subcommand being changed (for example `./c --list`, `./c --default <name>`, or `./c --whoami`).

## Architecture (big picture)
- **Single entrypoint**: `c` contains all logic (argument parsing, account resolution, account management, and final `exec claude` handoff).
- **Persistent state model**:
  - Global state directory: `~/.config/claude-accounts`
  - Registered account names: `~/.config/claude-accounts/accounts`
  - Default account: `~/.config/claude-accounts/default`
  - Per-account Claude config dir: `~/.config/claude-accounts/<account>`
- **Config sharing strategy**:
  - During `--add`, account dirs are scaffolded to symlink shared resources from `~/.claude` (`settings.json`, `CLAUDE.md`, `skills`, `plugins`).
  - `.claude.json` sharing is documented in `README.md` as part of the model.
  - Existing non-symlinked `skills`/`plugins` content in account dirs is migrated into `~/.claude` before replacing with symlinks.
- **Account selection flow**:
  - In normal launch mode, if `--account` is supplied, it is first written to local `.claude-account` in the current working directory.
  - Resolution then occurs by walking up directories for `.claude-account`, then falling back to global default, then interactive selection (`fzf` if available, otherwise Bash `select`).
- **Execution model**:
  - After resolution, launcher validates account dir existence and then runs: `CLAUDE_CONFIG_DIR=<account_dir> exec claude ...`
  - `--whoami` shells out to `claude auth status` and parses JSON via `python3`.

## External dependencies and assumptions
- Required: `bash`, `claude` CLI, `python3`.
- Optional UX dependency: `fzf` (used for interactive account selection when multiple accounts exist).
- `--remove` uses `sed -i ''`, which is macOS/BSD `sed` syntax.
