# Known syntax gaps — awaiting transpiler support

These tests document shell constructs the transpiler does not yet
handle correctly. The syntax matrix stays green; each file here is a
work item for the next iteration cycle.

- 021_negation.sh: `if ! cmd` — negation in if-condition is dropped
- 024_subshell.sh: `( cmds )` — subshell falls back to system() with
  broken quoting

Verification: bash <file> produces the expected output after #__EXPECT__;
the transpiled binary currently does not.
