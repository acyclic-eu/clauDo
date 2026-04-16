# clauDo

Multi-account [Claude Code](https://claude.ai/code) launcher. Tested on macOS. Switch between Claude accounts per project, with shared config via symlinks.

## How it works

Claude Code stores its config in `~/.claude/`. clauDo gives each account its own isolated directory (`~/.claude-<name>`) while symlinking shared config (settings, commands, CLAUDE.md) from a single source of truth.

Account resolution order:
1. `c <name>` positional arg or `--account <name>` flag
2. `.claude-account` file (walks up from current directory)
3. `~/.config/claude-accounts/default`
4. Interactive prompt (fzf if available, else `select`)

## Usage

```sh
c                        # launch Claude with resolved account
c work                   # implicit account (shorthand for --account work)
c -a <name>              # register + scaffold a new account (--add)
c -l                     # list all accounts (* = default) (--list)
c -d <name>              # set default account (--default)
c -w                     # print active account's email (--whoami)
c -t <name>              # use account for this session only (--temp)
c -r <name>              # remove an account (--remove)
```

Set a per-project account:
```sh
echo work > .claude-account
```

## Installation

Clone the repo (shallow) and add it to your PATH:

```sh
git clone --depth 1 https://github.com/acyclic-eu/clauDo.git ~/.local/bin/clauDo
```

Add to your shell config (`~/.zshrc`, `~/.bashrc`, etc.):

```sh
export PATH="$PATH:$HOME/.local/bin/clauDo"
```

Then add your first account — it becomes the default automatically:

```sh
c --add <your-account-name>
```

To update later:

```sh
git -C ~/.local/bin/clauDo pull
```

## Shared config

Items symlinked from `~/.claude/` into each account dir:
- `settings.json`
- `CLAUDE.md`
- `commands/`
- `plugins/`

`.claude.json` (MCP servers, auth) symlinks to `~/.claude.json`.
