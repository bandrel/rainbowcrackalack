/* CUDA-equivalent typedefs for OpenCL scalar types. */
typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef unsigned int   uint;
typedef unsigned long long ulong;

#ifndef IS_NV
#define IS_NV 1
#endif

__constant__ char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


/* Local-memory variant: first 5 position tables (45125 bytes) are in __shared__,
 * last 2 positions read from global.  This fits in the 48KB static __shared__
 * limit on NVIDIA compute_86 and eliminates most global memory latency. */
__device__ inline void index_to_plaintext_markov8_local(
    unsigned long long index,
    const unsigned char *sorted_pos0,
    const unsigned char *l_bigram,
    const unsigned char *g_bigram,
    unsigned char *plaintext)
{
    unsigned int indexHi = (unsigned int)(index / 81450625ULL);
    unsigned int indexLo = (unsigned int)(index - 81450625ULL * indexHi);

    unsigned short group_23 = (unsigned short)(indexLo / 9025);
    unsigned short group_01 = (unsigned short)(indexLo - (unsigned int)9025 * group_23);
    unsigned short group_67 = (unsigned short)(indexHi / 9025);
    unsigned short group_45 = (unsigned short)(indexHi - (unsigned int)9025 * group_67);

    unsigned char rank1 = (unsigned char)(group_01 / 95);
    unsigned char rank0 = (unsigned char)(group_01 - (unsigned short)95 * rank1);
    unsigned char rank3 = (unsigned char)(group_23 / 95);
    unsigned char rank2 = (unsigned char)(group_23 - (unsigned short)95 * rank3);
    unsigned char rank5 = (unsigned char)(group_45 / 95);
    unsigned char rank4 = (unsigned char)(group_45 - (unsigned short)95 * rank5);
    unsigned char rank7 = (unsigned char)(group_67 / 95);
    unsigned char rank6 = (unsigned char)(group_67 - (unsigned short)95 * rank7);

    /* Positions 0-4: read from shared memory (fast) */
    unsigned int ci = sorted_pos0[rank0];
    plaintext[0] = ci + 32;

    ci = l_bigram[ci * 95 + rank1];
    plaintext[1] = ci + 32;

    ci = l_bigram[9025 + ci * 95 + rank2];
    plaintext[2] = ci + 32;

    ci = l_bigram[18050 + ci * 95 + rank3];
    plaintext[3] = ci + 32;

    ci = l_bigram[27075 + ci * 95 + rank4];
    plaintext[4] = ci + 32;

    ci = l_bigram[36100 + ci * 95 + rank5];
    plaintext[5] = ci + 32;

    /* Positions 5-6: read from global memory (L2 cached) */
    ci = g_bigram[45125 + ci * 95 + rank6];
    plaintext[6] = ci + 32;

    ci = g_bigram[54150 + ci * 95 + rank7];
    plaintext[7] = ci + 32;
}

/* Original variant for kernels that don't use shared caching. */
__device__ inline void index_to_plaintext_markov8(
    unsigned long long index,
    const char *charset,
    unsigned int charset_len,
    const unsigned char *sorted_pos0,
    const unsigned char *sorted_bigram,
    unsigned char *plaintext)
{
    unsigned int indexHi = (unsigned int)(index / 81450625ULL);
    unsigned int indexLo = (unsigned int)(index - 81450625ULL * indexHi);

    unsigned short group_23 = (unsigned short)(indexLo / 9025);
    unsigned short group_01 = (unsigned short)(indexLo - (unsigned int)9025 * group_23);
    unsigned short group_67 = (unsigned short)(indexHi / 9025);
    unsigned short group_45 = (unsigned short)(indexHi - (unsigned int)9025 * group_67);

    unsigned char rank1 = (unsigned char)(group_01 / 95);
    unsigned char rank0 = (unsigned char)(group_01 - (unsigned short)95 * rank1);
    unsigned char rank3 = (unsigned char)(group_23 / 95);
    unsigned char rank2 = (unsigned char)(group_23 - (unsigned short)95 * rank3);
    unsigned char rank5 = (unsigned char)(group_45 / 95);
    unsigned char rank4 = (unsigned char)(group_45 - (unsigned short)95 * rank5);
    unsigned char rank7 = (unsigned char)(group_67 / 95);
    unsigned char rank6 = (unsigned char)(group_67 - (unsigned short)95 * rank7);

    unsigned int ci = sorted_pos0[rank0];
    plaintext[0] = ci + 32;

    ci = sorted_bigram[ci * 95 + rank1];
    plaintext[1] = ci + 32;

    ci = sorted_bigram[9025 + ci * 95 + rank2];
    plaintext[2] = ci + 32;

    ci = sorted_bigram[18050 + ci * 95 + rank3];
    plaintext[3] = ci + 32;

    ci = sorted_bigram[27075 + ci * 95 + rank4];
    plaintext[4] = ci + 32;

    ci = sorted_bigram[36100 + ci * 95 + rank5];
    plaintext[5] = ci + 32;

    ci = sorted_bigram[45125 + ci * 95 + rank6];
    plaintext[6] = ci + 32;

    ci = sorted_bigram[54150 + ci * 95 + rank7];
    plaintext[7] = ci + 32;
}


/*
 * MD4 OpenCL kernel based on Solar Designer's MD4 algorithm implementation at:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md4
 * This code is in public domain.
 */

#undef MD4_LUT3 /* No good for this format, just here for reference */

/* The basic MD4 functions */
#if MD4_LUT3
#define F(x, y, z)	lut3(x, y, z, 0xca)
#elif USE_BITSELECT
#define F(x, y, z)	bitselect((z), (y), (x))
#elif HAVE_ANDNOT
#define F(x, y, z)	((x & y) ^ ((~x) & z))
#else
#define F(x, y, z)	(z ^ (x & (y ^ z)))
#endif

#if MD4_LUT3
#define G(x, y, z)	lut3(x, y, z, 0xe8)
#else
#define G(x, y, z)	(((x) & ((y) | (z))) | ((y) & (z)))
#endif

#if MD4_LUT3
#define H(x, y, z)	lut3(x, y, z, 0x96)
#define H2 H
#else
#define H(x, y, z)	(((x) ^ (y)) ^ (z))
#define H2(x, y, z)	((x) ^ ((y) ^ (z)))
#endif

/* The MD4 transformation for all three rounds. */
#define STEP(f, a, b, c, d, x, s)	  \
	(a) += f((b), (c), (d)) + (x); \
	(a) = ((a << s) | (a >> (32 - s)))

__device__ inline void md4_encrypt(uint *hash, uint *W)
{
	hash[0] = 0x67452301;
	hash[1] = 0xefcdab89;
	hash[2] = 0x98badcfe;
	hash[3] = 0x10325476;

	/* Round 1 */
	STEP(F, hash[0], hash[1], hash[2], hash[3], W[0], 3);
	STEP(F, hash[3], hash[0], hash[1], hash[2], W[1], 7);
	STEP(F, hash[2], hash[3], hash[0], hash[1], W[2], 11);
	STEP(F, hash[1], hash[2], hash[3], hash[0], W[3], 19);
	STEP(F, hash[0], hash[1], hash[2], hash[3], W[4], 3);
	STEP(F, hash[3], hash[0], hash[1], hash[2], W[5], 7);
	STEP(F, hash[2], hash[3], hash[0], hash[1], W[6], 11);
	STEP(F, hash[1], hash[2], hash[3], hash[0], W[7], 19);
	STEP(F, hash[0], hash[1], hash[2], hash[3], W[8], 3);
	STEP(F, hash[3], hash[0], hash[1], hash[2], W[9], 7);
	STEP(F, hash[2], hash[3], hash[0], hash[1], W[10], 11);
	STEP(F, hash[1], hash[2], hash[3], hash[0], W[11], 19);
	STEP(F, hash[0], hash[1], hash[2], hash[3], W[12], 3);
	STEP(F, hash[3], hash[0], hash[1], hash[2], W[13], 7);
	STEP(F, hash[2], hash[3], hash[0], hash[1], W[14], 11);
	STEP(F, hash[1], hash[2], hash[3], hash[0], W[15], 19);

	/* Round 2 */
	STEP(G, hash[0], hash[1], hash[2], hash[3], W[0] + 0x5a827999, 3);
	STEP(G, hash[3], hash[0], hash[1], hash[2], W[4] + 0x5a827999, 5);
	STEP(G, hash[2], hash[3], hash[0], hash[1], W[8] + 0x5a827999, 9);
	STEP(G, hash[1], hash[2], hash[3], hash[0], W[12] + 0x5a827999, 13);
	STEP(G, hash[0], hash[1], hash[2], hash[3], W[1] + 0x5a827999, 3);
	STEP(G, hash[3], hash[0], hash[1], hash[2], W[5] + 0x5a827999, 5);
	STEP(G, hash[2], hash[3], hash[0], hash[1], W[9] + 0x5a827999, 9);
	STEP(G, hash[1], hash[2], hash[3], hash[0], W[13] + 0x5a827999, 13);
	STEP(G, hash[0], hash[1], hash[2], hash[3], W[2] + 0x5a827999, 3);
	STEP(G, hash[3], hash[0], hash[1], hash[2], W[6] + 0x5a827999, 5);
	STEP(G, hash[2], hash[3], hash[0], hash[1], W[10] + 0x5a827999, 9);
	STEP(G, hash[1], hash[2], hash[3], hash[0], W[14] + 0x5a827999, 13);
	STEP(G, hash[0], hash[1], hash[2], hash[3], W[3] + 0x5a827999, 3);
	STEP(G, hash[3], hash[0], hash[1], hash[2], W[7] + 0x5a827999, 5);
	STEP(G, hash[2], hash[3], hash[0], hash[1], W[11] + 0x5a827999, 9);
	STEP(G, hash[1], hash[2], hash[3], hash[0], W[15] + 0x5a827999, 13);

	/* Round 3 */
	STEP(H, hash[0], hash[1], hash[2], hash[3], W[0] + 0x6ed9eba1, 3);
	STEP(H2, hash[3], hash[0], hash[1], hash[2], W[8] + 0x6ed9eba1, 9);
	STEP(H, hash[2], hash[3], hash[0], hash[1], W[4] + 0x6ed9eba1, 11);
	STEP(H2, hash[1], hash[2], hash[3], hash[0], W[12] + 0x6ed9eba1, 15);
	STEP(H, hash[0], hash[1], hash[2], hash[3], W[2] + 0x6ed9eba1, 3);
	STEP(H2, hash[3], hash[0], hash[1], hash[2], W[10] + 0x6ed9eba1, 9);
	STEP(H, hash[2], hash[3], hash[0], hash[1], W[6] + 0x6ed9eba1, 11);
	STEP(H2, hash[1], hash[2], hash[3], hash[0], W[14] + 0x6ed9eba1, 15);
	STEP(H, hash[0], hash[1], hash[2], hash[3], W[1] + 0x6ed9eba1, 3);
	STEP(H2, hash[3], hash[0], hash[1], hash[2], W[9] + 0x6ed9eba1, 9);
	STEP(H, hash[2], hash[3], hash[0], hash[1], W[5] + 0x6ed9eba1, 11);
	STEP(H2, hash[1], hash[2], hash[3], hash[0], W[13] + 0x6ed9eba1, 15);
	STEP(H, hash[0], hash[1], hash[2], hash[3], W[3] + 0x6ed9eba1, 3);
	STEP(H2, hash[3], hash[0], hash[1], hash[2], W[11] + 0x6ed9eba1, 9);
	STEP(H, hash[2], hash[3], hash[0], hash[1], W[7] + 0x6ed9eba1, 11);
	STEP(H2, hash[1], hash[2], hash[3], hash[0], W[15] + 0x6ed9eba1, 15);

	hash[0] = hash[0] + 0x67452301;
	hash[1] = hash[1] + 0xefcdab89;
	hash[2] = hash[2] + 0x98badcfe;
	hash[3] = hash[3] + 0x10325476;
}


__device__ inline unsigned long long hash_ntlm8(unsigned char *plaintext) {
  unsigned int key[16] = {0};
  unsigned int output[4];

  for (int i = 0; i < 4; i++)
    key[i] = plaintext[i * 2] | (plaintext[(i * 2) + 1] << 16);

  key[4] = 0x80;
  key[14] = 0x80;

  md4_encrypt(output, key);

  return ((unsigned long long)output[1]) << 32 | (unsigned long long)output[0];
}


__device__ inline unsigned long long hash_to_index_markov8(unsigned long long hash, unsigned int reduction_offset, unsigned int pos) {
  return (hash + reduction_offset + pos) % 6634204312890625ULL;
}


__device__ inline unsigned long long hash_char_to_index_markov8(unsigned char *hash_value, unsigned int reduction_offset, unsigned int pos) {
  unsigned long long ret = hash_value[7];
  ret <<= 8;
  ret |= hash_value[6];
  ret <<= 8;
  ret |= hash_value[5];
  ret <<= 8;
  ret |= hash_value[4];
  ret <<= 8;
  ret |= hash_value[3];
  ret <<= 8;
  ret |= hash_value[2];
  ret <<= 8;
  ret |= hash_value[1];
  ret <<= 8;
  ret |= hash_value[0];

  return hash_to_index_markov8(ret, reduction_offset, pos);
}
