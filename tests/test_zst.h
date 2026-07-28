#ifndef _TEST_ZST_H
#define _TEST_ZST_H
int test_zst(void);
int test_zst_decompress_rejects_undersized_content_size(void);
int test_zst_compress_roundtrip(void);
int test_zst_compress_declares_size(void);
int test_zst_compress_rejects_bad_size(void);
int test_zst_compress_short_read_guard(void);
#endif
