#!/usr/bin/env bash
# Bash tab completion for crackalack binaries.
# Source this file or place it in /etc/bash_completion.d/ or
# $(brew --prefix)/etc/bash_completion.d/ on macOS.

_crackalack_hashes() {
    echo "ntlm lm netntlmv1"
}

_crackalack_charsets() {
    echo "ascii-32-95 alpha numeric loweralpha loweralpha-numeric mixalpha mixalpha-numeric loweralpha-numeric-space"
}

_crackalack_gen() {
    local cur prev words cword
    _init_completion || return

    case "$cword" in
        1)
            # <hash>
            COMPREPLY=( $(compgen -W "$(_crackalack_hashes)" -- "$cur") )
            return
            ;;
        2)
            # <charset_or_mask>
            COMPREPLY=( $(compgen -W "$(_crackalack_charsets)" -- "$cur") )
            return
            ;;
        3|4)
            # <min_len> <max_len>: integers 1-16
            COMPREPLY=( $(compgen -W "$(seq 1 16)" -- "$cur") )
            return
            ;;
    esac

    # Handle flags that can appear anywhere after positional args
    case "$prev" in
        -gws)
            # Takes an integer; no useful completion
            return
            ;;
        --markov)
            # Takes a .markov file
            _filedir markov
            return
            ;;
    esac

    # For remaining positional args (table_index, chain_len, num_chains, part)
    # and flag completion
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "-gws --markov" -- "$cur") )
    fi
}

_crackalack_lookup() {
    local cur prev words cword
    _init_completion || return

    case "$prev" in
        --markov)
            # Optionally takes a .markov file
            _filedir markov
            return
            ;;
    esac

    case "$cword" in
        1)
            # <table_dir>: directory containing .rt or .rtc files
            _filedir -d
            return
            ;;
        2)
            # <hash_file>: text file
            _filedir
            return
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "--markov" -- "$cur") )
    fi
}

_crackalack_verify() {
    local cur prev words cword
    _init_completion || return

    # Accepts one or more .rt or .rtc files
    _filedir '@(rt|rtc)'
}

_crackalack_sort() {
    local cur prev words cword
    _init_completion || return

    case "$prev" in
        --jobs)
            # Takes an integer; no useful completion
            return
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "--jobs" -- "$cur") )
        return
    fi

    # .rt files
    _filedir rt
}

_crackalack_plan() {
    local cur prev words cword
    _init_completion || return

    local subcommands="estimate recommend train"

    if [[ "$cword" -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "$subcommands" -- "$cur") )
        return
    fi

    local subcmd="${words[1]}"
    case "$subcmd" in
        estimate|recommend)
            case "$cword" in
                2)
                    COMPREPLY=( $(compgen -W "$(_crackalack_hashes)" -- "$cur") )
                    ;;
                3)
                    COMPREPLY=( $(compgen -W "$(_crackalack_charsets)" -- "$cur") )
                    ;;
                4|5)
                    COMPREPLY=( $(compgen -W "$(seq 1 16)" -- "$cur") )
                    ;;
                # 6 = num_chains, 7 = optional target_success_fraction: no completion
            esac
            ;;
        train)
            # <wordlist_file>
            _filedir
            ;;
    esac
}

_crackalack_rtc2rt() {
    local cur prev words cword
    _init_completion || return

    _filedir rtc
}

_get_chain() {
    local cur prev words cword
    _init_completion || return

    _filedir rt
}

_enumerate_chain() {
    local cur prev words cword
    _init_completion || return

    _filedir rt
}

_perfectify() {
    local cur prev words cword
    _init_completion || return

    _filedir rt
}

complete -F _crackalack_gen      crackalack_gen
complete -F _crackalack_lookup   crackalack_lookup
complete -F _crackalack_verify   crackalack_verify
complete -F _crackalack_sort     crackalack_sort
complete -F _crackalack_plan     crackalack_plan
complete -F _crackalack_rtc2rt   crackalack_rtc2rt
complete -F _get_chain           get_chain
complete -F _enumerate_chain     enumerate_chain
complete -F _perfectify          perfectify
# crackalack_unit_tests takes no arguments; no completion needed
