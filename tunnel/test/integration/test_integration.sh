#!/bin/bash
# SSH Tunnel Integration Test Script
# Tests local TCP port forwarding through an SSH bastion to an on-network service

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"
SSH_HOST="127.0.0.1"
SSH_PORT="2222"
SSH_USER="root"
LOCAL_PORT="9000"
REMOTE_HOST="http-service"
REMOTE_PORT="8000"
TUNNEL_CLIENT_BINARY="ssh_tunnel_client"
TEST_KEY_FILE="${SCRIPT_DIR}/ci_test_key"
EXPECTED_RESPONSE="Hello from service"
TUNNEL_SETUP_TIMEOUT=3
HTTP_TIMEOUT=2

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."
    
    # Kill tunnel client if running
    if [ -n "${TUNNEL_PID:-}" ] && kill -0 "$TUNNEL_PID" 2>/dev/null; then
        log_info "Killing tunnel client (PID: $TUNNEL_PID)"
        kill "$TUNNEL_PID" 2>/dev/null || true
    fi
    
    # Stop Docker containers
    if [ -f "$COMPOSE_FILE" ]; then
        log_info "Stopping Docker containers..."
        docker-compose -f "$COMPOSE_FILE" down --remove-orphans 2>/dev/null || true
    fi
    
    # Remove test key files
    if [ -f "$TEST_KEY_FILE" ]; then
        rm -f "$TEST_KEY_FILE" "$TEST_KEY_FILE.pub"
    fi
}

# Set up trap for cleanup
trap cleanup EXIT

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if Docker is available
    if ! command -v docker >/dev/null 2>&1; then
        log_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    # Check if docker-compose is available
    if ! command -v docker-compose >/dev/null 2>&1; then
        log_error "docker-compose is not installed or not in PATH"
        exit 1
    fi
    
    # Check if tunnel client binary exists
    if [ ! -f "$TUNNEL_CLIENT_BINARY" ]; then
        log_error "Tunnel client binary not found: $TUNNEL_CLIENT_BINARY"
        log_info "Please build the tunnel extension first:"
        log_info "  cd tunnel && make build"
        log_info "  cp build/ssh_tunnel_client test/integration/"
        exit 1
    fi
    
    # Check if curl is available for HTTP testing
    if ! command -v curl >/dev/null 2>&1; then
        log_error "curl is not installed or not in PATH"
        exit 1
    fi
    
    log_success "All prerequisites satisfied"
}

# Generate SSH keypair
generate_ssh_key() {
    log_info "Generating SSH keypair..."
    
    if [ -f "$TEST_KEY_FILE" ]; then
        log_warning "Test key already exists, removing..."
        rm -f "$TEST_KEY_FILE" "$TEST_KEY_FILE.pub"
    fi
    
    ssh-keygen -t rsa -b 2048 -N "" -f "$TEST_KEY_FILE" -C "ci-test-key"
    
    if [ ! -f "$TEST_KEY_FILE" ] || [ ! -f "$TEST_KEY_FILE.pub" ]; then
        log_error "Failed to generate SSH keypair"
        exit 1
    fi
    
    log_success "SSH keypair generated successfully"
}

# Start Docker services
start_services() {
    log_info "Starting Docker services..."
    
    # Start the services
    docker-compose -f "$COMPOSE_FILE" up -d
    
    # Wait for services to be ready
    log_info "Waiting for services to be ready..."
    
    # Wait for SSH bastion
    local ssh_ready=false
    for i in {1..30}; do
        if docker exec ssh-bastion pgrep -f "sshd" >/dev/null 2>&1; then
            ssh_ready=true
            break
        fi
        sleep 1
    done
    
    if [ "$ssh_ready" = false ]; then
        log_error "SSH bastion failed to start"
        docker-compose -f "$COMPOSE_FILE" logs ssh-bastion
        exit 1
    fi
    
    # Wait for HTTP service
    local http_ready=false
    for i in {1..30}; do
        if docker exec http-service sh -c "cat /proc/*/comm 2>/dev/null | grep -q python" 2>/dev/null; then
            http_ready=true
            break
        fi
        sleep 1
    done
    
    if [ "$http_ready" = false ]; then
        log_error "HTTP service failed to start"
        docker-compose -f "$COMPOSE_FILE" logs http-service
        exit 1
    fi
    
    log_success "All services started successfully"
}

# Inject SSH public key into bastion
inject_ssh_key() {
    log_info "Injecting SSH public key into bastion..."
    
    # Copy public key to bastion container
    docker cp "$TEST_KEY_FILE.pub" ssh-bastion:/tmp/ci_test_key.pub
    
    # Wait for the setup script to process the key
    log_info "Waiting for SSH key setup..."
    for i in {1..10}; do
        if docker exec ssh-bastion test -f /root/.ssh/authorized_keys; then
            log_success "SSH key injected successfully"
            return 0
        fi
        sleep 1
    done
    
    log_error "Failed to inject SSH key"
    docker-compose -f "$COMPOSE_FILE" logs ssh-bastion
    exit 1
}

# Test SSH connectivity
test_ssh_connectivity() {
    log_info "Testing SSH connectivity..."
    
    # Test SSH connection with key authentication
    if ssh -i "$TEST_KEY_FILE" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p "$SSH_PORT" "$SSH_USER@$SSH_HOST" "echo 'SSH connection successful'" 2>/dev/null; then
        log_success "SSH connectivity verified"
    else
        log_error "SSH connectivity test failed"
        exit 1
    fi
}

# Start tunnel client
start_tunnel() {
    log_info "Starting tunnel client..."
    
    # Check if local port is already in use
    if netstat -tuln 2>/dev/null | grep -q ":$LOCAL_PORT "; then
        log_error "Local port $LOCAL_PORT is already in use"
        exit 1
    fi
    
    # Start tunnel client in background
    "$TUNNEL_CLIENT_BINARY" \
        --host "$SSH_HOST" \
        --port "$SSH_PORT" \
        --user "$SSH_USER" \
        --key "$TEST_KEY_FILE" \
        --lport "$LOCAL_PORT" \
        --rhost "$REMOTE_HOST" \
        --rport "$REMOTE_PORT" &
    
    TUNNEL_PID=$!
    log_info "Tunnel client started with PID: $TUNNEL_PID"
    
    # Wait for tunnel to be established
    log_info "Waiting for tunnel to be established (timeout: ${TUNNEL_SETUP_TIMEOUT}s)..."
    local tunnel_ready=false
    for i in $(seq 1 $((TUNNEL_SETUP_TIMEOUT * 10))); do
        if netstat -tuln 2>/dev/null | grep -q ":$LOCAL_PORT "; then
            tunnel_ready=true
            break
        fi
        sleep 0.1
    done
    
    if [ "$tunnel_ready" = false ]; then
        log_error "Tunnel failed to establish within ${TUNNEL_SETUP_TIMEOUT} seconds"
        if [ -n "${TUNNEL_PID:-}" ]; then
            kill "$TUNNEL_PID" 2>/dev/null || true
        fi
        exit 1
    fi
    
    log_success "Tunnel established successfully"
}

# Test HTTP connectivity through tunnel
test_http_connectivity() {
    log_info "Testing HTTP connectivity through tunnel..."
    
    # Perform HTTP GET request through tunnel
    local response
    response=$(curl -s --max-time "$HTTP_TIMEOUT" "http://127.0.0.1:$LOCAL_PORT" 2>/dev/null || echo "")
    
    if [ "$response" = "$EXPECTED_RESPONSE" ]; then
        log_success "HTTP connectivity test passed"
        log_success "Received expected response: '$response'"
    else
        log_error "HTTP connectivity test failed"
        log_error "Expected: '$EXPECTED_RESPONSE'"
        log_error "Received: '$response'"
        exit 1
    fi
}

# Main test execution
main() {
    log_info "Starting SSH tunnel integration test"
    log_info "Test configuration:"
    log_info "  SSH Host: $SSH_HOST:$SSH_PORT"
    log_info "  SSH User: $SSH_USER"
    log_info "  Local Port: $LOCAL_PORT"
    log_info "  Remote Host: $REMOTE_HOST:$REMOTE_PORT"
    log_info "  Expected Response: '$EXPECTED_RESPONSE'"
    
    check_prerequisites
    generate_ssh_key
    start_services
    inject_ssh_key
    test_ssh_connectivity
    start_tunnel
    test_http_connectivity
    
    log_success "All tests passed! SSH tunnel integration test completed successfully."
}

# Run main function
main "$@" 