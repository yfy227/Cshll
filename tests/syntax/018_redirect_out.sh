#!/bin/bash
tmpf=$(mktemp)
echo "saved data" > "$tmpf"
cat "$tmpf"
rm -f "$tmpf"
#__EXPECT__
saved data
