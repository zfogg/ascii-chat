#!/bin/bash
set -euo pipefail

# ASCII-Chat Coturn STUN/TURN Server Deployment Script
# This script deploys coturn to /opt/ascii-chat on the server

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/ascii-chat/deploy/acds/coturn"
SERVICE_FILE="coturn.service"
SERVICE_PATH="/etc/systemd/system/${SERVICE_FILE}"

echo "========================================="
echo "ASCII-Chat Coturn Deployment"
echo "========================================="

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root"
   exit 1
fi

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed"
    exit 1
fi

# Check if Docker Compose is installed
if ! command -v docker-compose &> /dev/null; then
    echo "Error: Docker Compose is not installed"
    exit 1
fi

# Step 1: Ensure installation directory exists
echo "Step 1: Creating installation directory..."
mkdir -p "$INSTALL_DIR"

# Step 2: Copy configuration files (if not already in place)
echo "Step 2: Ensuring configuration files are in place..."
if [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
    echo "Copying files from $SCRIPT_DIR to $INSTALL_DIR..."
    cp -v "${SCRIPT_DIR}/docker-compose.yml" "$INSTALL_DIR/"
    cp -v "${SCRIPT_DIR}/turnserver.conf" "$INSTALL_DIR/"
else
    echo "Already running from installation directory, skipping copy..."
fi
chmod 644 "$INSTALL_DIR/turnserver.conf"

# Step 3: Install systemd service
echo "Step 3: Installing systemd service..."
cp -v "${SCRIPT_DIR}/${SERVICE_FILE}" "$SERVICE_PATH"
chmod 644 "$SERVICE_PATH"

# Step 4: Reload systemd
echo "Step 4: Reloading systemd daemon..."
systemctl daemon-reload

# Step 5: Enable service (start on boot)
echo "Step 5: Enabling coturn service..."
systemctl enable coturn.service

# Step 6: Start service
echo "Step 6: Starting coturn service..."
systemctl start coturn.service

# Step 7: Check status
echo ""
echo "========================================="
echo "Deployment Complete!"
echo "========================================="
echo ""
echo "Service Status:"
systemctl status coturn.service --no-pager || true
echo ""
echo "Container Status:"
cd "$INSTALL_DIR" && docker-compose ps
echo ""
echo "Listening Ports:"
ss -tulnp | grep -E ':(3478|5349|49152)' || echo "No coturn ports detected yet (container may still be starting)"
echo ""
echo "========================================="
echo "Next Steps:"
echo "========================================="
echo "1. Update DNS records:"
echo "   - stun.ascii-chat.com -> server IP"
echo "   - turn.ascii-chat.com -> server IP"
echo ""
echo "2. Open firewall ports:"
echo "   - 3478 UDP/TCP (STUN/TURN)"
echo "   - 5349 UDP/TCP (STUN/TURN over TLS)"
echo "   - 49152-65535 UDP (TURN relay ports)"
echo ""
echo "3. Change default credentials in:"
echo "   $INSTALL_DIR/turnserver.conf"
echo "   Then restart: systemctl restart coturn"
echo ""
echo "4. (Optional) Configure TLS certificates for TURNS"
echo ""
echo "Logs: journalctl -u coturn -f"
echo "========================================="
