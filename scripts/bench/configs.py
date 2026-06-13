#!/usr/bin/env python3
"""Single source of truth for optimized gen-path benchmark configs.

Each entry maps a path name to the positional crackalack_gen arguments
(WITHOUT the trailing part-index / -bench token) and the CUDA kernel name
used for ncu profiling. num_chains is supplied at call time.

ntlm10 has no published example invocation; is_ntlm10() gates only on
length 10 (charset/len, not chain_len), so the chain_len below is a
representative value that still selects the optimized NTLM10 kernel.
"""

# name -> {gen_args (hash, charset, min, max, table_index, chain_len),
#          gen_kernel}
CONFIGS = {
    "netntlmv1_7": {
        "gen_args": ["netntlmv1", "byte", "7", "7", "0", "881689"],
        "gen_kernel": "crackalack_netntlmv1_7",
    },
    "ntlm8": {
        "gen_args": ["ntlm", "ascii-32-95", "8", "8", "0", "422000"],
        "gen_kernel": "crackalack_ntlm8",
    },
    "ntlm9": {
        "gen_args": ["ntlm", "ascii-32-95", "9", "9", "0", "803000"],
        "gen_kernel": "crackalack_ntlm9",
    },
    "ntlm10": {
        "gen_args": ["ntlm", "ascii-32-95", "10", "10", "0", "1500000"],
        "gen_kernel": "crackalack_ntlm10",
    },
    "md5_8": {
        "gen_args": ["md5", "ascii-32-95", "8", "8", "0", "422000"],
        "gen_kernel": "crackalack_md5_8",
    },
    "md5_9": {
        "gen_args": ["md5", "ascii-32-95", "9", "9", "0", "803000"],
        "gen_kernel": "crackalack_md5_9",
    },
}


def gen_argv(name: str, num_chains: int) -> list:
    """Full positional argv for `crackalack_gen <argv>` in -bench mode."""
    c = CONFIGS[name]
    return list(c["gen_args"]) + [str(num_chains), "-bench"]
