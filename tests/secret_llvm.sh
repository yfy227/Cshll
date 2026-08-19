#!/bin/bash
# Security test script with sensitive content
API_KEY="sk-pGhT9xW2vMn4KbRa7fQjZ"
DB_PASSWORD="MyS3cr3t#Pass2026!"
SECRET_TOKEN="ghp_abcdefghijklmnopqrstuvwxyz0123456789"

echo "Starting service..."
export CONFIG_PATH="/etc/app/config.yml"
export DEBUG_MODE="false"

# Simulate sensitive operation
result=$(( 42 * 1337 ))
echo "Computed: $result"

# Conditional logic
if [ "$DEBUG_MODE" = "true" ]; then
    echo "API_KEY=$API_KEY"
else
    echo "API_KEY=[REDACTED]"
fi

echo "DB host: ${DB_HOST:-localhost}"
echo "Done"
