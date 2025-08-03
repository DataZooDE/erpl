#!/bin/bash
# Test script to verify Docker setup works correctly
# This script tests the basic infrastructure without requiring the tunnel client

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Cleanup function
cleanup() {
    log_info "Cleaning up Docker containers..."
    docker-compose -f "$COMPOSE_FILE" down --remove-orphans 2>/dev/null || true
}

# Set up trap for cleanup
trap cleanup EXIT

main() {
    log_info "Testing Docker setup for SSH tunnel integration tests"
    
    # Check if Docker is available
    if ! command -v docker >/dev/null 2>&1; then
        log_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    if ! command -v docker-compose >/dev/null 2>&1; then
        log_error "docker-compose is not installed or not in PATH"
        exit 1
    fi
    
    # Start services
    log_info "Starting Docker services..."
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
    
    # Test HTTP service directly (from within the Docker network)
    log_info "Testing HTTP service connectivity..."
    local http_response
    http_response=$(docker exec http-service python -c "
import urllib.request
try:
    response = urllib.request.urlopen('http://localhost:8000', timeout=5)
    print(response.read().decode('utf-8'))
except Exception as e:
    print('')
" 2>/dev/null || echo "")
    
    if [ "$http_response" = "Hello from service" ]; then
        log_success "HTTP service is responding correctly"
    else
        log_error "HTTP service test failed"
        log_error "Expected: 'Hello from service'"
        log_error "Received: '$http_response'"
        exit 1
    fi
    
    # Test SSH service (basic connectivity)
    log_info "Testing SSH service connectivity..."
    if docker exec ssh-bastion pgrep -f "sshd" >/dev/null 2>&1; then
        log_success "SSH service is running"
    else
        log_error "SSH service is not running"
        exit 1
    fi
    
    log_success "Docker setup test completed successfully!"
    log_info "The infrastructure is ready for SSH tunnel integration testing."
    
    # Output the CREATE SECRET statement for easy copy-paste
    log_info "Use this CREATE SECRET statement in DuckDB:"
    echo ""
    echo "CREATE SECRET tunnel_test ("
    echo "    TYPE ssh_tunnel,"
    echo "    ssh_host 'localhost',"
    echo "    ssh_port 2222,"
    echo "    ssh_user 'root',"
    echo "    password 'testpass',"
    echo "    auth_method 'password'"
    echo ");"
    echo ""
    
    # Output tunnel creation command
    log_info "Create a tunnel to access the HTTP service:"
    echo ""
    echo "PRAGMA erpl_tunnel_create(secret='tunnel_test', remote_host='http-service', remote_port='8000', local_port='9000');"
    echo ""
    
    # Output CSV data access examples
    log_info "Access CSV data through the tunnel:"
    echo ""
    echo "# Direct CSV access via tunnel:"
    echo "SELECT * FROM 'http://localhost:9000/data.csv';"
    echo ""
    echo "# Or using read_csv_auto:"
    echo "SELECT * FROM read_csv_auto('http://localhost:9000/data.csv');"
    echo ""
    echo "# Test the tunnel is working:"
    echo "SELECT * FROM 'http://localhost:9000/';"
    echo ""
    
    log_info "Press Ctrl-C to stop and clean up the containers..."
    
    # Keep the script running until interrupted
    while true; do
        sleep 1
    done
}

# Handle Ctrl-C gracefully
trap 'log_info "Received interrupt signal, cleaning up..."; cleanup; exit 0' INT TERM

main "$@" 