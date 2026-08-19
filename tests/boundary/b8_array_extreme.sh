#!/bin/bash
# 边界场景8: 数组边界 — 空数组/负索引/稀疏/多维

# 1. 空数组遍历
empty=()
for e in "${empty[@]}"; do
    echo "should_not_print: $e"
done
echo "empty_done"

# 2. 数组切片边界
arr=(a b c d e f g h i j)
echo "slice0=${arr[@]:0:0}"
echo "slice_neg=${arr[@]: -3}"
echo "slice_oob=${arr[@]:100:3}"
echo "slice_zero_len=${arr[@]:2:0}"

# 3. 负索引边界
echo "neg1=${arr[-1]}"
echo "neg_full=${arr[@]: -1}"

# 4. 稀疏数组+遍历
sparse[0]=a
sparse[3]=b
sparse[7]=c
echo "sparse_count=${#sparse[@]}"
echo "sparse_idx=${!sparse[@]}"

# 5. 数组赋值到已存在
arr2=(x y z)
arr2=(a b c d)
echo "reassign_count=${#arr2[@]}"
echo "reassign_all=${arr2[@]}"

# 6. 数组长度 vs 字符串长度
arr3=(hello world test)
echo "arr_len=${#arr3[@]}"
echo "elem0_len=${#arr3[0]}"
echo "elem1_len=${#arr3[1]}"

# 7. 关联数组边界
declare -A m
echo "empty_assoc=${#m[@]}"
m[key1]=val1
m[key2]=val2
echo "assoc_keys=${!m[@]}"
echo "assoc_vals=${m[@]}"

# 8. unset数组元素
arr4=(a b c d e)
unset 'arr4[2]'
echo "after_unset=${arr4[@]}"
echo "after_unset_count=${#arr4[@]}"
