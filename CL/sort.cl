__kernel void bitonic_sort_step(
    __global ulong *data,
    __global const uint *k_val,
    __global const uint *j_val)
{
  uint i = get_global_id(0);
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
