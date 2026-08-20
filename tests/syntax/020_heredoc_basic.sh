#!/bin/bash
cat <<EOF
line one $HOME_MARKER
line two
EOF
#__EXPECT__
line one 
line two
