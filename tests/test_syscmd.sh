#!/bin/bash
# Test system command passthrough

# Network commands
echo "=== Network ==="
echo "test" | nc -l 12345 &
sleep 0.1
echo "nc works"

# File commands
echo "=== File ops ==="
tar czf /tmp/test_$$.tar.gz /etc/hostname 2>/dev/null
echo "tar exit: $?"
gzip -c /etc/hostname > /tmp/test_$$.gz 2>/dev/null
echo "gzip exit: $?"
file /etc/hostname 2>/dev/null | head -1
stat -c %s /etc/hostname 2>/dev/null

# System info
echo "=== System ==="
free -m 2>/dev/null | head -1
uptime 2>/dev/null | head -1
uname -r 2>/dev/null
df -h / 2>/dev/null | head -2

# Process commands
echo "=== Process ==="
ps aux 2>/dev/null | head -1
pgrep -f test 2>/dev/null | head -1

# User commands
echo "=== User ==="
id 2>/dev/null
who 2>/dev/null | head -1

# Misc
echo "=== Misc ==="
which bash 2>/dev/null
whereis bash 2>/dev/null | head -1
md5sum /etc/hostname 2>/dev/null | head -1
sha256sum /etc/hostname 2>/dev/null | head -1

echo "SYSTEM COMMANDS TEST DONE"
