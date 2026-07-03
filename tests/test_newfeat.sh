#!/bin/bash
# Test new features: brace expansion, process substitution, (( ))

# Brace expansion - comma list
echo "brace list:"
echo {a,b,c}
echo pre{fix,fix,suffix}post
echo file.{txt,sh,c}

# Brace expansion - numeric range
echo "numeric range:"
echo {1..5}
echo item{1..3}

# Brace expansion - alpha range
echo "alpha range:"
echo {x..z}

# (( )) arithmetic test
x=5
if (( x > 3 )); then
    echo "x > 3 via (( ))"
fi
if (( x == 5 )); then
    echo "x == 5 via (( ))"
fi

# (( )) with assignment
(( y = 10 * 2 ))
echo "y = $y"

# Process substitution
echo "process sub:"
diff <(echo "a") <(echo "a") && echo "same"
cat <(echo "hello") <(echo "world")

# Combined features
for i in {1..3}; do
    echo "loop $i"
done

echo "NEW FEATURES DONE"
