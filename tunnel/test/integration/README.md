# SSH Tunnel Integration Test Setup

This directory contains a complete test environment for testing SSH tunnel functionality with DuckDB.

## Overview

The test environment consists of:
- **SSH Bastion Host**: A Docker container running SSH server on port 2222
- **HTTP Service**: A Python HTTP server serving CSV test data on port 8000 (internal)
- **Docker Network**: Isolated network for secure communication

## Quick Start

1. **Start the test environment:**
   ```bash
   ./test_setup.sh
   ```

2. **Copy the provided commands into DuckDB:**
   - The script will output a `CREATE SECRET` statement
   - It will also show tunnel creation and data access commands

3. **Test the tunnel:**
   - Create the secret and tunnel as shown in the output
   - Access CSV data through the tunnel using the provided SQL commands

4. **Stop the environment:**
   - Press `Ctrl-C` to stop the script and clean up containers

## HTTP Service Endpoints

The HTTP service provides the following endpoints:

- `GET /` - Hello message: "Hello from service"
- `GET /data.csv` - Sample CSV data with columns: id, name, city, age
- `GET /status` - Health check endpoint: "OK"

## Sample CSV Data

The HTTP server serves this sample data:
```csv
id,name,city,age
1,Alice,New York,30
2,Bob,Los Angeles,25
3,Charlie,Chicago,35
4,Diana,Boston,28
5,Eve,Seattle,32
```

## DuckDB Usage Examples

### 1. Create SSH Secret (Password Authentication)
```sql
CREATE SECRET tunnel_test_password (
    TYPE ssh_tunnel,
    ssh_host 'localhost',
    ssh_port '2222',
    ssh_user 'root',
    password 'testpass',
    auth_method 'password'
);
```

### 2. Create SSH Secret (Key Authentication - No Passphrase)
```sql
CREATE SECRET tunnel_test_key (
    TYPE ssh_tunnel,
    ssh_host 'localhost',
    ssh_port '2222',
    ssh_user 'root',
    private_key_path 'test/integration/test_key_static',
    auth_method 'key'
);
```

### 3. Create SSH Secret (Key Authentication - With Passphrase)
```sql
CREATE SECRET tunnel_test_key_passphrase (
    TYPE ssh_tunnel,
    ssh_host 'localhost',
    ssh_port '2222',
    ssh_user 'root',
    private_key_path 'test/integration/test_key_static_passphrase',
    passphrase 'testpassphrase',
    auth_method 'key'
);
```

### 4. Create Tunnels
```sql
-- Using password authentication
PRAGMA erpl_tunnel_create(secret='tunnel_test_password', remote_host='http-service', remote_port='8000', local_port='9000');

-- Using key authentication (no passphrase)
PRAGMA erpl_tunnel_create(secret='tunnel_test_key', remote_host='http-service', remote_port='8000', local_port='9001');

-- Using key authentication (with passphrase)
PRAGMA erpl_tunnel_create(secret='tunnel_test_key_passphrase', remote_host='http-service', remote_port='8000', local_port='9002');
```

### 5. Access CSV Data Through Tunnels
```sql
-- Direct CSV access via password tunnel
SELECT * FROM 'http://localhost:9000/data.csv';

-- Direct CSV access via key tunnel (no passphrase)
SELECT * FROM 'http://localhost:9001/data.csv';

-- Direct CSV access via key tunnel (with passphrase)
SELECT * FROM 'http://localhost:9002/data.csv';

-- Or using read_csv_auto
SELECT * FROM read_csv_auto('http://localhost:9000/data.csv');

-- Test the tunnels are working
SELECT * FROM 'http://localhost:9000/';
SELECT * FROM 'http://localhost:9001/';
SELECT * FROM 'http://localhost:9002/';
```

### 6. Close Tunnels
```sql
PRAGMA erpl_tunnel_close(1);  -- Close password tunnel
PRAGMA erpl_tunnel_close(2);  -- Close key tunnel (no passphrase)
PRAGMA erpl_tunnel_close(3);  -- Close key tunnel (with passphrase)
```

## Authentication Methods

The test environment supports both password and key-based SSH authentication:

### Password Authentication
- **Username**: `root`
- **Password**: `testpass`
- **Method**: Password-based authentication
- **Use case**: Simple testing and development

### Key Authentication (No Passphrase)
- **Username**: `root`
- **Key Type**: RSA 2048-bit
- **Key File**: `test_key_static`
- **Method**: Public key authentication
- **Use case**: Simple key authentication

### Key Authentication (With Passphrase)
- **Username**: `root`
- **Key Type**: RSA 2048-bit
- **Key File**: `test_key_static_passphrase`
- **Passphrase**: `testpassphrase`
- **Method**: Public key authentication with passphrase
- **Use case**: More secure key authentication

The SSH bastion is configured to accept all authentication methods simultaneously.

## Architecture

```
┌─────────────────┐    SSH Tunnel    ┌─────────────────┐
│   DuckDB        │ ───────────────► │  SSH Bastion    │
│  (localhost)    │                  │  (localhost:2222)│
│                 │                  │  (Password + Key)│
└─────────────────┘                  └─────────────────┘
                                              │
                                              │ Docker Network
                                              ▼
                                     ┌─────────────────┐
                                     │  HTTP Service   │
                                     │ (http-service:8000)│
                                     └─────────────────┘
```

## Network Configuration

- **SSH Bastion**: Accessible on `localhost:2222`
- **HTTP Service**: Only accessible within Docker network as `http-service:8000`
- **Tunnel**: Forwards `localhost:9000` → `http-service:8000` through SSH

## Troubleshooting

1. **SSH Connection Issues**: Ensure port 2222 is not in use
2. **HTTP Access Issues**: Verify the tunnel is created successfully
3. **Container Issues**: Check Docker logs with `docker-compose logs`

## Manual Testing

You can also test the HTTP service directly from within the Docker network:
```bash
docker exec http-service python -c "
import urllib.request
print(urllib.request.urlopen('http://localhost:8000/data.csv').read().decode())
" 