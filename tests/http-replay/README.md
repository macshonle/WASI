# HTTP Replay Test Files

This directory contains JSONL (JSON Lines) files for HTTP capture/replay testing in safe mode.

## Purpose

When running with `WASI_SAFE_MODE` enabled, the HTTP implementation uses these pre-recorded
HTTP interactions instead of making real network requests. This allows:

- Testing all code paths without network access
- Deterministic test results
- Testing edge cases that are hard to reproduce with live servers
- Running tests in sandboxed environments

## File Format

Each line in a `.jsonl` file is a complete JSON object representing an HTTP interaction.

### Success Response Format

```json
{
  "id": "unique_identifier",
  "request": {
    "method": "GET",
    "url": "http://example.com/path",
    "headers": {"Header-Name": "value"},
    "body": "base64_encoded_body"
  },
  "response": {
    "status": 200,
    "headers": {"Content-Type": "text/plain"},
    "body": "base64_encoded_body"
  }
}
```

### Chunked Response Format

```json
{
  "id": "chunked_response",
  "request": {...},
  "response": {
    "status": 200,
    "headers": {"Transfer-Encoding": "chunked"},
    "chunks": ["base64_chunk1", "base64_chunk2"],
    "trailers": {"X-Checksum": "value"}
  }
}
```

### Error Response Format

```json
{
  "id": "connection_error",
  "request": {...},
  "error": {
    "code": "connection_refused",
    "message": "Connection refused"
  }
}
```

## Fields

### Request Fields
- `id` (required): Unique identifier for matching requests
- `method` (required): HTTP method (GET, POST, PUT, DELETE, etc.)
- `url` (required): Full URL including scheme, host, and path
- `headers` (optional): Object of header name-value pairs
- `body` (optional): Base64-encoded request body

### Response Fields
- `status` (required): HTTP status code
- `headers` (optional): Object of header name-value pairs (can have array values for multi-value headers)
- `body` (optional): Base64-encoded response body
- `chunks` (optional): Array of base64-encoded chunks (for chunked transfer encoding)
- `trailers` (optional): Object of trailer name-value pairs

### Error Fields
- `code` (required): WASI HTTP error code name (see below)
- `message` (optional): Human-readable error message

## Error Codes

The following error codes can be used in the `error.code` field:

| Code | Description |
|------|-------------|
| `dns_error` | DNS resolution failed |
| `connection_refused` | TCP connection refused |
| `connection_timeout` | TCP connection timed out |
| `connection_read_timeout` | Read operation timed out |
| `connection_write_timeout` | Write operation timed out |
| `http_request_uri_invalid` | Invalid request URI |
| `http_response_incomplete` | Response body truncated |
| `http_response_transfer_coding` | Malformed chunked encoding |
| `tls_protocol_error` | TLS handshake failure |
| `tls_certificate_error` | Certificate verification failed |
| `internal_error` | Internal implementation error |

## Files

| File | Description |
|------|-------------|
| `basic_operations.jsonl` | Simple GET/POST success cases |
| `error_conditions.jsonl` | Various error codes |
| `chunked_responses.jsonl` | Chunked transfer encoding |
| `edge_cases.jsonl` | Large bodies, special headers, redirects |

## Matching Algorithm

The replay system matches requests by:
1. HTTP method (exact match)
2. URL (exact match, including query string)
3. Required headers (if specified in replay file)

The first matching entry is used. If no match is found, an `internal_error` is returned.

## Usage

```bash
# Run tests in safe mode with replay
WASI_HTTP_REPLAY_FILE=tests/http-replay/basic_operations.jsonl make test-http-safe
```

## Adding New Test Cases

1. Add a new line to the appropriate `.jsonl` file
2. Ensure the `id` is unique within the file
3. Base64-encode any binary or special content
4. Test with both success and error scenarios
