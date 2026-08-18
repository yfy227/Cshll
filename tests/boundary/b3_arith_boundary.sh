#!/bin/bash
# 边界场景测试3: 算术边界
echo "=== B3: arithmetic boundary ==="

# 除零
echo "div_zero=$((10/0))"
echo "mod_zero=$((10%0))"

# 负数运算
echo "neg=$(( -5 ))"
echo "neg_add=$(( -5 + 3 ))"
echo "neg_mul=$(( -5 * -3 ))"
echo "neg_div=$(( -10 / 3 ))"

# 溢出边界
echo "big=$(( 2147483647 ))"
echo "big_add=$(( 2147483647 + 1 ))"
echo "mul_overflow=$(( 100000 * 100000 ))"

# 位运算
echo "shift=$(( 1 << 31 ))"
echo "and=$(( 255 & 15 ))"
echo "or=$(( 128 | 64 ))"
echo "xor=$(( 255 ^ 15 ))"
echo "not=$(( ~0 ))"

# 自增边界
x=0
((x++))
((x++))
echo "incr=$x"
((x--))
echo "decr=$x"
