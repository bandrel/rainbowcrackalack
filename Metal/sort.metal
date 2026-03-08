#include <metal_stdlib>
using namespace metal;

kernel void bitonic_sort_step(
    device ulong *data         [[buffer(0)]],
    device const uint *k_val   [[buffer(1)]],
    device const uint *j_val   [[buffer(2)]],
    uint i                     [[thread_position_in_grid]])
{
  uint k = *k_val;
  uint j = *j_val;
  uint l = i ^ j;
  if (l <= i) return;

  ulong end_i = data[i * 2 + 1];
  ulong end_l = data[l * 2 + 1];
  int ascending = ((i & k) == 0);

  if ((ascending && end_i > end_l) || (!ascending && end_i < end_l)) {
    ulong tmp0 = data[i * 2];
    ulong tmp1 = data[i * 2 + 1];
    data[i * 2]     = data[l * 2];
    data[i * 2 + 1] = data[l * 2 + 1];
    data[l * 2]     = tmp0;
    data[l * 2 + 1] = tmp1;
  }
}
