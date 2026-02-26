/* Stub for netntlmv1 - the full DES-based NetNTLMv1 implementation from
 * netntlmv1.cl (3130 lines) has not been translated to Metal yet.
 * This stub allows compilation when HASH_TYPE != HASH_NETNTLMV1.
 * If NetNTLMv1 support is needed on Metal, translate the full CL/netntlmv1.cl file. */

#ifndef _NETNTLMV1_METAL
#define _NETNTLMV1_METAL

#if HASH_TYPE == HASH_NETNTLMV1
#error "NetNTLMv1 Metal shader not yet implemented. Translate CL/netntlmv1.cl to Metal/netntlmv1.metal."
#endif

#endif
