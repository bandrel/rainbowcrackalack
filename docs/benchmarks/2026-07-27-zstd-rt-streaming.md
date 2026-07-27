## Level sweep

| level | threads | raw bytes | comp bytes | ratio | compress s | compress MB/s | compress RSS | decompress s | decompress MB/s | decompress RSS | roundtrip_ok |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | 16000000 | 10526581 | 1.520 | 0.216 | 74.16 | 7667712 | 0.036 | 438.36 | 23576576 | **TRUE** |
| 3 | 0 | 16000000 | 10522533 | 1.521 | 0.078 | 204.29 | 9977856 | 0.030 | 527.90 | 25149440 | **TRUE** |
| 9 | 0 | 16000000 | 9871331 | 1.621 | 0.243 | 65.91 | 21839872 | 0.030 | 524.68 | 27213824 | **TRUE** |
| 12 | 0 | 16000000 | 9882924 | 1.619 | 0.624 | 25.65 | 53297152 | 0.029 | 559.17 | 27197440 | **TRUE** |
| 19 | 0 | 16000000 | 8728071 | 1.833 | 4.303 | 3.72 | 100040704 | 0.033 | 483.62 | 31342592 | **TRUE** |
| 22 | 0 | 16000000 | 8728069 | 1.833 | 4.037 | 3.96 | 292077568 | 0.034 | 476.03 | 38764544 | **TRUE** |
| 19 | 16 | 16000000 | 8728070 | 1.833 | 4.272 | 3.74 | 116785152 | 0.036 | 449.63 | 31342592 | **TRUE** |

## Format comparison (crackalack_lookup end-to-end)

| format | lookup s | lookup RSS | cracked |
|---|---|---|---|
| rt | 0.024 | 11632640 | True |
| rtc | 0.023 | 11632640 | True |
| zst | 0.023 | 11632640 | True |

`.rti2` has no in-tree writer (only `rti2_decompress`), so it is excluded from this comparison.
