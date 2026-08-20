#!/bin/bash
true && echo "and works"
false || echo "or works"
true && false || echo "chain works"
#__EXPECT__
and works
or works
chain works
