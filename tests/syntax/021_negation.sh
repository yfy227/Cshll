#!/bin/bash
if ! false; then
  echo "not-false is true"
fi
! true || echo "negation with or"
#__EXPECT__
not-false is true
negation with or
