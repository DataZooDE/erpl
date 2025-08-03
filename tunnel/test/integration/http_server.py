#!/usr/bin/env python3
"""
Simple HTTP server for SSH tunnel integration testing.
Serves a static response and CSV test data to verify tunnel connectivity.
Supports range requests for DuckDB compatibility.
"""

import http.server
import socketserver
import sys
import os
import re

PORT = 8000
RESPONSE = "Hello from service"

# Sample CSV data for testing
CSV_DATA = """id,name,city,age
1,Alice,New York,30
2,Bob,Los Angeles,25
3,Charlie,Chicago,35
4,Diana,Boston,28
5,Eve,Seattle,32
"""

class TestHTTPRequestHandler(http.server.BaseHTTPRequestHandler):
    def parse_range_header(self, range_header, content_length):
        """Parse Range header and return start, end positions."""
        if not range_header:
            return 0, content_length - 1
        
        # Parse "bytes=start-end" format
        match = re.match(r'bytes=(\d+)-(\d+)?', range_header)
        if not match:
            return 0, content_length - 1
        
        start = int(match.group(1))
        end_str = match.group(2)
        
        if end_str:
            end = int(end_str)
        else:
            end = content_length - 1
        
        # Ensure valid range
        if start >= content_length:
            return 0, 0
        if end >= content_length:
            end = content_length - 1
        
        return start, end
    
    def send_range_response(self, data, content_type):
        """Send response with proper range request support."""
        content_length = len(data)
        range_header = self.headers.get('Range')
        
        if range_header:
            start, end = self.parse_range_header(range_header, content_length)
            if start > end:
                # Invalid range
                self.send_response(416)  # Range Not Satisfiable
                self.send_header('Content-Range', f'bytes */{content_length}')
                self.end_headers()
                return
            
            # Send 206 Partial Content
            self.send_response(206)
            self.send_header('Content-Type', content_type)
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Range', f'bytes {start}-{end}/{content_length}')
            self.send_header('Content-Length', str(end - start + 1))
            self.end_headers()
            self.wfile.write(data[start:end+1])
        else:
            # Send full content
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Length', str(content_length))
            self.end_headers()
            self.wfile.write(data)
    
    def do_GET(self):
        """Handle GET requests with different responses based on path."""
        path = self.path
        
        if path == "/":
            # Root path - serve the hello message
            data = RESPONSE.encode('utf-8')
            self.send_range_response(data, 'text/plain')
        
        elif path == "/data.csv":
            # CSV data endpoint with range request support
            data = CSV_DATA.encode('utf-8')
            self.send_range_response(data, 'text/csv')
        
        elif path == "/status":
            # Status endpoint for health checks
            data = "OK".encode('utf-8')
            self.send_range_response(data, 'text/plain')
        
        else:
            # 404 for unknown paths
            self.send_response(404)
            self.send_header('Content-type', 'text/plain')
            error_msg = f"Not found: {path}"
            self.send_header('Content-length', str(len(error_msg)))
            self.end_headers()
            self.wfile.write(error_msg.encode('utf-8'))
    
    def log_message(self, format, *args):
        """Override to provide better logging."""
        sys.stderr.write(f"[HTTP Server] {format % args}\n")

def main():
    """Start the HTTP server."""
    try:
        with socketserver.TCPServer(("", PORT), TestHTTPRequestHandler) as httpd:
            print(f"[HTTP Server] Starting server on port {PORT}")
            print(f"[HTTP Server] Available endpoints:")
            print(f"[HTTP Server]   GET / - Hello message")
            print(f"[HTTP Server]   GET /data.csv - Sample CSV data")
            print(f"[HTTP Server]   GET /status - Health check")
            print(f"[HTTP Server]   CSV data preview:")
            for line in CSV_DATA.strip().split('\n')[:3]:  # Show first 3 lines
                print(f"[HTTP Server]     {line}")
            print(f"[HTTP Server]     ...")
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[HTTP Server] Shutting down...")
    except Exception as e:
        print(f"[HTTP Server] Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 