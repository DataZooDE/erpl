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

### 1. Create SSH Secret
```sql
CREATE SECRET tunnel_test (
    TYPE tunnel_ssh,
    ssh_host 'localhost',
    ssh_port '2222',
    ssh_user 'root',
    password 'testpass',
    auth_method 'password'
);
```

### 2. Create Tunnel
```sql
PRAGMA tunnel_create(secret='tunnel_test', remote_host='http-service', remote_port='8000', local_port='9000');
```

### 3. Access CSV Data Through Tunnel
```sql
-- Direct CSV access via tunnel
SELECT * FROM 'http://localhost:9000/data.csv';

-- Or using read_csv_auto
SELECT * FROM read_csv_auto('http://localhost:9000/data.csv');

-- Test the tunnel is working
SELECT * FROM 'http://localhost:9000/';
```

### 4. Close Tunnel
```sql
PRAGMA tunnel_close(tunnel_id='1');
```

## Architecture

```
┌─────────────────┐    SSH Tunnel    ┌─────────────────┐
│   DuckDB        │ ───────────────► │  SSH Bastion    │
│  (localhost)    │                  │  (localhost:2222)│
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