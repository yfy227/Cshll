#!/bin/bash
echo "escaped \"quote\""
echo "escaped \\ backslash"
echo 'dollar: $HOME'
#__EXPECT__
escaped "quote"
escaped \ backslash
dollar: $HOME
