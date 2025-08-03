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
echo "[SSH Setup] Root password is set to: testpass"

# Keep the container running
echo "[SSH Setup] SSH setup complete, keeping container alive..."
while true; do
    sleep 3600
done 