#!/bin/bash
check() {
  if [ "$1" == "ok" ]; then
    return 0
  fi
  return 1
}
check ok && echo "passed"
check bad || echo "failed-as-expected"
#__EXPECT__
passed
failed-as-expected
