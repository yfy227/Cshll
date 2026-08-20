#!/bin/bash
a="abc"
b="abd"
if [[ "$a" < "$b" ]]; then
  echo "a before b"
fi
if [[ "$a" != "$b" ]]; then
  echo "different"
fi
#__EXPECT__
a before b
different
