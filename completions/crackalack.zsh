#compdef crackalack_gen crackalack_lookup crackalack_verify crackalack_sort crackalack_plan crackalack_unit_tests crackalack_rtc2rt get_chain enumerate_chain perfectify
# Zsh tab completion for crackalack binaries.
# Place this file in a directory on your $fpath and run `compinit`.

local -a _crackalack_hashes=(ntlm lm netntlmv1)
local -a _crackalack_charsets=(
    ascii-32-95
    alpha
    numeric
    loweralpha
    'loweralpha-numeric'
    mixalpha
    'mixalpha-numeric'
    'loweralpha-numeric-space'
)
local -a _len_values=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)

_crackalack_gen() {
    _arguments \
        '1:hash:($_crackalack_hashes)' \
        '2:charset or mask:($_crackalack_charsets)' \
        '3:min length:($_len_values)' \
        '4:max length:($_len_values)' \
        '5:table index: ' \
        '6:chain length: ' \
        '7:num chains: ' \
        '8:part: ' \
        '-gws[global work size]:N: ' \
        '--markov[Markov model file]:markov file:_files -g "*.markov"'
}

_crackalack_lookup() {
    _arguments \
        '1:table directory:_files -/' \
        '2:hash file:_files' \
        '--markov[Markov model file]:markov file:_files -g "*.markov"'
}

_crackalack_verify() {
    _arguments \
        '*:rainbow table file:_files -g "*.(rt|rtc)"'
}

_crackalack_sort() {
    _arguments \
        '--jobs[parallel jobs]:N: ' \
        '*:rainbow table file:_files -g "*.rt"'
}

_crackalack_plan_estimate_recommend() {
    _arguments \
        '1:hash:($_crackalack_hashes)' \
        '2:charset or mask:($_crackalack_charsets)' \
        '3:min length:($_len_values)' \
        '4:max length:($_len_values)' \
        '5:num chains: ' \
        '6:target success fraction: '
}

_crackalack_plan() {
    local -a subcommands=(
        'estimate:estimate coverage for given parameters'
        'recommend:recommend table parameters'
        'train:train Markov model from wordlist'
    )

    if (( CURRENT == 2 )); then
        _describe 'subcommand' subcommands
        return
    fi

    local subcmd="$words[2]"
    case "$subcmd" in
        estimate|recommend)
            # Shift past the subcommand and complete remaining args
            (( CURRENT-- ))
            shift words
            _crackalack_plan_estimate_recommend
            ;;
        train)
            _arguments '2:wordlist file:_files'
            ;;
    esac
}

_crackalack_rtc2rt() {
    _arguments \
        '*:compressed table file:_files -g "*.rtc"'
}

_get_chain() {
    _arguments \
        '*:rainbow table file:_files -g "*.rt"'
}

_enumerate_chain() {
    _arguments \
        '*:rainbow table file:_files -g "*.rt"'
}

_perfectify() {
    _arguments \
        '*:rainbow table file:_files -g "*.rt"'
}

# Dispatch to the correct completion function based on the command name.
case "$service" in
    crackalack_gen)       _crackalack_gen ;;
    crackalack_lookup)    _crackalack_lookup ;;
    crackalack_verify)    _crackalack_verify ;;
    crackalack_sort)      _crackalack_sort ;;
    crackalack_plan)      _crackalack_plan ;;
    crackalack_unit_tests) : ;;  # no arguments
    crackalack_rtc2rt)    _crackalack_rtc2rt ;;
    get_chain)            _get_chain ;;
    enumerate_chain)      _enumerate_chain ;;
    perfectify)           _perfectify ;;
esac
