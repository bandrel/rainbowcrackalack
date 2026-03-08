# Shell Tab Completion for crackalack

Tab completion scripts for all crackalack binaries, supporting bash and zsh.

Binaries covered: `crackalack_gen`, `crackalack_lookup`, `crackalack_verify`,
`crackalack_sort`, `crackalack_plan`, `crackalack_unit_tests`, `crackalack_rtc2rt`,
`get_chain`, `enumerate_chain`, `perfectify`.

## Bash

### Linux

Copy to the system completion directory:

```bash
sudo cp crackalack.bash /etc/bash_completion.d/crackalack
```

Or source it from your shell profile for a single user:

```bash
echo 'source /path/to/crackalack.bash' >> ~/.bashrc
source ~/.bashrc
```

### macOS (Homebrew bash)

```bash
cp crackalack.bash "$(brew --prefix)/etc/bash_completion.d/crackalack"
```

Ensure your `~/.bash_profile` or `~/.bashrc` loads Homebrew completions:

```bash
[[ -r "$(brew --prefix)/etc/profile.d/bash_completion.sh" ]] && \
    source "$(brew --prefix)/etc/profile.d/bash_completion.sh"
```

## Zsh

The zsh script uses the `#compdef` mechanism and must be placed in a directory
on `$fpath` before `compinit` is called.

### Linux and macOS

```bash
# Pick any directory already on $fpath, or add a new one:
mkdir -p ~/.zsh/completions
cp crackalack.zsh ~/.zsh/completions/_crackalack
```

Add this to your `~/.zshrc` **before** `compinit`:

```zsh
fpath=(~/.zsh/completions $fpath)
autoload -Uz compinit && compinit
```

Reload your shell:

```zsh
exec zsh
```

### macOS (Homebrew zsh)

Homebrew installs a site-functions directory on `$fpath` automatically:

```bash
cp crackalack.zsh "$(brew --prefix)/share/zsh/site-functions/_crackalack"
```

Then reload completions:

```zsh
rm -f ~/.zcompdump && exec zsh
```
