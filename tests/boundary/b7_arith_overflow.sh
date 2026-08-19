#!/bin/bash
# 边界场景7: 算术边界 — 溢出/除零/负数/混合

# 1. 64位边界
echo "max64=$((9223372036854775807))"
echo "max64_plus1=$((9223372036854775807 + 1))"
echo "min64=$((-9223372036854775808))"

# 2. 除零变体
x=0
echo "safe_div=$((10 / x))" 2>/dev/null || echo "caught div zero"

# 3. 负数取模
echo "neg_mod=$(((-7) % 3))"
echo "pos_mod=$((7 % (-3)))"

# 4. 幂运算边界
echo "pow0=$((2 ** 0))"
echo "pow1=$((2 ** 1))"
echo "pow62=$((2 ** 62)))"

# 5. 移位边界
echo "shl31=$((1 << 31))"
echo "shl63=$((1 << 63))"

# 6. 负数位移
echo "neg_shr=$((-8 >> 2))"

# 7. 复合赋值链
x=10
((x += 5, x *= 2, x -= 3))
echo "chain=$x"

# 8. 自增在表达式中
y=5
echo "incr_in_expr=$((y++ + ++y))"
echo "y=$y"

# 9. 浮点格式化
printf "%.4f\n" $(echo "scale=4; 22/7" | bc)

# 10. 零值测试
z=0
echo "zero_test=$((z == 0))"
echo "zero_neg=$((z < 0))"
echo "zero_pos=$((z > 0))"
