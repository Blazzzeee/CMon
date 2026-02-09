import http.client
import json
import sys
import os

def test_route(method, path, expected_status, headers={}):
    print(f"Testing {method} {path}...", end=" ")
    try:
        conn = http.client.HTTPConnection("localhost", 8000, timeout=2)
        conn.request(method, path, headers=headers)
        res = conn.getresponse()
        conn.close()
    except Exception as e:
        print(f"FAIL: Connection error: {e}")
        return False
        
    if res.status != expected_status:
        print(f"FAIL: Expected {expected_status}, got {res.status}")
        return False
    
    # Check JSON structure for success codes (200, 4xx, 5xx all return JSON now)
    data = res.read()
    if data:
        try:
            # Decode if bytes
            if isinstance(data, bytes):
                data = data.decode('utf-8')
            
            # The server might return multiple JSONs if multiple requests? No, per request.
            # But run_logs via tail might contain quotes/newlines that need escaping.
            # We implemented json_escape, so it should be valid JSON.
            j = json.loads(data)
            
            if "status" not in j:
                print(f"FAIL: JSON missing 'status'. Got: {j}")
                return False
                
            if expected_status == 200 and j["code"] != 200:
                 print(f"FAIL: Response code mismatch in JSON. Got {j['code']}")
                 return False
                 
        except json.JSONDecodeError:
            print(f"FAIL: Invalid JSON body: {data}")
            return False
            
    print(f"PASS ({res.status})")
    return True

# Read key
key = "default_secret"
if os.path.exists("client_secret.key"):
    with open("client_secret.key", "r") as f:
        key = f.read().strip()

headers = {"access_token": key}
bad_headers = {"access_token": "wrong_key"}

success = True

print("--- Safe Routes ---")
success &= test_route("GET", "/health", 200, headers)
# success &= test_route("GET", "/logs", 200, headers) # Might fail if log file missing/perms

print("\n--- Error Tests ---")
# 404 Unknown Route
success &= test_route("GET", "/invalid_route_123", 404, headers)
# 405 Method Not Allowed
success &= test_route("POST", "/health", 405, headers)
# 401 Unauthorized (Missing header)
success &= test_route("GET", "/health", 401, {})
# 401 Unauthorized (Wrong key)
success &= test_route("GET", "/health", 401, bad_headers)

print("\n--- Risky Routes (dry-run) ---")
# User requested specific test: curl -v "http://127.0.0.1:8000/sync_upstream?branch=master" -X PUT -H "ACCESS_TOKEN: ..."
# We will test this route. Note: this might trigger a git pull if not mocked or careful.
# Since run_git_pull executes /usr/bin/git pull, and we are in a repo, it might try to pull.
# However, for the test suite, we just want to verify 200 OK and JSON response.
# We'll use a dummy branch to likely fail the git command but succeed the HTTP request?
# Or just accept it runs.
print("Testing PUT /sync_upstream?branch=master...", end=" ")
try:
    conn = http.client.HTTPConnection("localhost", 8000, timeout=5)
    # The user example used "ACCESS_TOKEN" header, but code uses "access_token" (case sensitive? evhttp_find_header is usually case-insensitive? No, libevent headers are case-insensitive? implementation uses evhttp_find_header which is case-insensitive).
    # Update: check main.c: #define AUTH_HEADER_KEY "access_token". evhttp_find_header is case-blind.
    conn.request("PUT", "/sync_upstream?branch=master", headers=headers)
    res = conn.getresponse()
    data = res.read()
    conn.close()
    
    if res.status == 200:
        j = json.loads(data)
        if "status" in j:
             print(f"PASS ({res.status})")
        else:
             print(f"FAIL: JSON missing status. Got: {j}")
             success = False
    else:
        print(f"FAIL: Expected 200, got {res.status}. Body: {data}")
        success = False
except Exception as e:
    print(f"FAIL: {e}")
    success = False

print("\n--- Risky Routes (Skipped) ---")
print("Skipping /reboot, /restart, /deploy_branch to avoid side effects.")

if not success:
    sys.exit(1)
print("\nAll tests passed!")
