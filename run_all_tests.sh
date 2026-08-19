#!/bin/bash
# Run all 4 test suites and save results
cd /home/z/my-project/Cshll

echo "=== test_stress ===" > /tmp/audit_results.txt
bash tests/test_stress.sh >> /tmp/audit_results.txt 2>&1

echo "" >> /tmp/audit_results.txt
echo "=== test_stress2 ===" >> /tmp/audit_results.txt
bash tests/test_stress2.sh >> /tmp/audit_results.txt 2>&1

echo "" >> /tmp/audit_results.txt
echo "=== test_stress3 ===" >> /tmp/audit_results.txt
bash tests/test_stress3.sh >> /tmp/audit_results.txt 2>&1

echo "" >> /tmp/audit_results.txt
echo "=== test_deep_audit ===" >> /tmp/audit_results.txt
bash tests/test_deep_audit.sh >> /tmp/audit_results.txt 2>&1

echo "" >> /tmp/audit_results.txt
echo "=== ALL DONE ===" >> /tmp/audit_results.txt
