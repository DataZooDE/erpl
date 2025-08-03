#!/bin/bash
# Setup script for SSH bastion container
# This script configures SSH for both password and key authentication

set -e

echo "[SSH Setup] Starting SSH configuration..."

# Create .ssh directory for root
mkdir -p /root/.ssh
chmod 700 /root/.ssh

# Set root password explicitly
echo "[SSH Setup] Setting root password..."
echo "root:testpass" | chpasswd

# Add static public keys to authorized_keys
echo "[SSH Setup] Adding static public keys to authorized_keys..."
if [ -f /tmp/test_key_static.pub ]; then
    cat /tmp/test_key_static.pub >> /root/.ssh/authorized_keys
    echo "[SSH Setup] Added test_key_static.pub to authorized_keys"
fi

if [ -f /tmp/test_key_static_passphrase.pub ]; then
    cat /tmp/test_key_static_passphrase.pub >> /root/.ssh/authorized_keys
    echo "[SSH Setup] Added test_key_static_passphrase.pub to authorized_keys"
fi

chmod 600 /root/.ssh/authorized_keys

# Also wait for any dynamic public keys to be injected (from test scripts)
echo "[SSH Setup] Waiting for dynamic SSH key setup..."
for i in {1..30}; do
    if [ -f /tmp/ci_test_key.pub ] && [ -f /tmp/ci_test_key_passphrase.pub ]; then
        echo "[SSH Setup] Found dynamic public keys, adding to authorized_keys..."
        cat /tmp/ci_test_key.pub >> /root/.ssh/authorized_keys
        cat /tmp/ci_test_key_passphrase.pub >> /root/.ssh/authorized_keys
        chmod 600 /root/.ssh/authorized_keys
        echo "[SSH Setup] Dynamic public keys added to authorized_keys"
        break
    fi
    sleep 1
done

# SSH configuration is now handled by the custom sshd_config file
echo "[SSH Setup] Using custom SSH configuration..."

# Verify SSH daemon is running with our custom config
echo "[SSH Setup] Verifying SSH daemon status..."
if pgrep sshd > /dev/null; then
    echo "[SSH Setup] SSH daemon is running"
else
    echo "[SSH Setup] Starting SSH daemon with custom config..."
    /usr/sbin/sshd -D -f /etc/ssh/sshd_config &
fi

echo "[SSH Setup] SSH password authentication enabled"
echo "[SSH Setup] SSH key authentication enabled"
echo "[SSH Setup] Root password is set to: testpass"

# Keep the container running
echo "[SSH Setup] SSH setup complete, keeping container alive..."
while true; do
    sleep 3600
done 