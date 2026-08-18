#!/bin/bash
# 边界场景测试4: 数组边界
echo "=== B4: array boundary ==="

# 空数组
arr=()
echo "empty_count=${#arr[@]}"
echo "empty_first=${arr[0]}"

# 单元素
arr2=("one")
echo "single_count=${#arr2[@]}"
echo "single_0=${arr2[0]}"
echo "single_1=${arr2[1]}"

# 负索引
arr3=(a b c d e)
echo "neg_idx=${arr3[-1]}"
echo "neg_idx2=${arr3[-2]}"

# 越界访问
echo "oob=${arr3[100]}"
echo "oob_neg=${arr3[-100]}"

# 稀疏数组
arr4[0]="zero"
arr4[5]="five"
arr4[10]="ten"
echo "sparse_count=${#arr4[@]}"
echo "sparse_0=${arr4[0]}"
echo "sparse_3=${arr4[3]}"
echo "sparse_5=${arr4[5]}"

# 追加
arr5=(a b c)
arr5+=("d")
arr5+=("e" "f")
echo "append_count=${#arr5[@]}"
echo "append_all=${arr5[@]}"
