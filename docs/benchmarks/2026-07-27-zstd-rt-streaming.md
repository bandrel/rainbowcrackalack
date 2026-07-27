## Level sweep

| level | threads | raw bytes | comp bytes | ratio | compress s | compress MB/s | compress RSS | decompress s | decompress MB/s | decompress RSS | roundtrip_ok |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | 16000000 | 10526581 | 1.520 | 0.314 | 50.90 | 7667712 | 0.035 | 456.77 | 23576576 | **TRUE** |
| 3 | 0 | 16000000 | 10522533 | 1.521 | 0.076 | 211.60 | 9994240 | 0.038 | 417.31 | 25165824 | **TRUE** |
| 9 | 0 | 16000000 | 9871331 | 1.621 | 0.238 | 67.29 | 21856256 | 0.031 | 523.25 | 27246592 | **TRUE** |
| 12 | 0 | 16000000 | 9882924 | 1.619 | 0.651 | 24.57 | 53297152 | 0.032 | 498.12 | 27230208 | **TRUE** |
| 19 | 0 | 16000000 | 8728071 | 1.833 | 4.471 | 3.58 | 100040704 | 0.033 | 486.70 | 31358976 | **TRUE** |
| 22 | 0 | 16000000 | 8728069 | 1.833 | 4.164 | 3.84 | 292077568 | 0.033 | 479.46 | 38780928 | **TRUE** |
| 19 | 16 | 16000000 | 8728070 | 1.833 | 4.333 | 3.69 | 116719616 | 0.034 | 472.31 | 31342592 | **TRUE** |

## Format comparison (crackalack_lookup end-to-end)

| format | lookup s | lookup RSS | cracked |
|---|---|---|---|
| rt | 0.968 | 38256640 | True |
| rtc | 0.770 | 47382528 | True |
| zst | 0.788 | 47431680 | True |

`.rti2` has no in-tree writer (only `rti2_decompress`), so it is excluded from this comparison.
