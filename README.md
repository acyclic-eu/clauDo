# clauDo

Multi-account [Claude Code](https://claude.ai/code) launcher. Switch between Claude accounts per project, with shared config via symlinks.

## How it works

Claude Code stores its config in `~/.claude/`. clauDo gives each account its own isolated directory (`~/.claude-<name>`) while symlinking shared config (settings, commands, CLAUDE.md) from a single source of truth.

Account resolution order:
1. `--account <name>` flag
2. `.claude-account` file (walks up from current directory)
3. `~/.config/claude-accounts/default`
4. Interactive prompt (fzf if available, else `select`)

## Usage

```sh
c                        # launch Claude with resolved account
c --account work         # explicit account
c --add <name>           # register + scaffold a new account
c --list                 # list all accounts (* = default)
c --whoami               # print active account's email domain
```

Set a per-project account:
```sh
echo work > .claude-account
```

## Installation

```sh
cp c ~/.local/bin/c
chmod +x ~/.local/bin/c
```

Then add your first account — it becomes the default automatically:
```sh
c --add <your-account-name>
```

## Shared config

Items symlinked from `~/.claude/` into each account dir:
- `settings.json`
- `CLAUDE.md`
- `commands/`
- `plugins/`

`.claude.json` (MCP servers, auth) symlinks to `~/.claude.json`.
