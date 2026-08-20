#!/bin/bash
i=0
i=$((i + 1))
i=$((i * 10))
i=$((i - 2))
i=$((i / 2))
echo "i=$i"
#__EXPECT__
i=4
