/**
 * WASI HTTP Mock Server
 *
 * A simple HTTP server for testing the WASI HTTP implementation.
 * Provides various endpoints for testing different HTTP scenarios.
 *
 * Usage:
 *   node mock_server.js [port]
 *
 * Default port: 18080
 */

const http = require('http');
const url = require('url');

const DEFAULT_PORT = 18080;
const port = parseInt(process.argv[2], 10) || DEFAULT_PORT;

/**
 * Route handlers
 */
const routes = {
  // Simple GET endpoint
  'GET /simple': (req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('Hello from mock server');
  },

  // Echo POST body back
  'POST /echo': (req, res) => {
    const body = [];
    req.on('data', chunk => body.push(chunk));
    req.on('end', () => {
      const data = Buffer.concat(body);
      res.writeHead(200, {
        'Content-Type': req.headers['content-type'] || 'application/octet-stream',
        'X-Received-Length': String(data.length)
      });
      res.end(data);
    });
  },

  // Return request headers
  'GET /headers': (req, res) => {
    res.writeHead(200, {
      'Content-Type': 'application/json',
      'X-Custom': 'test-value'
    });
    res.end(JSON.stringify({
      received_headers: req.headers
    }));
  },

  // Multi-value headers
  'GET /multi-headers': (req, res) => {
    res.writeHead(200, {
      'Content-Type': 'text/plain',
      'Set-Cookie': ['session=abc123; Path=/', 'tracking=xyz789; Path=/; HttpOnly']
    });
    res.end('Check headers');
  },

  // Chunked response
  'GET /chunked': (req, res) => {
    res.writeHead(200, {
      'Content-Type': 'text/plain',
      'Transfer-Encoding': 'chunked'
    });
    res.write('chunk1\n');
    setTimeout(() => {
      res.write('chunk2\n');
      setTimeout(() => {
        res.write('chunk3\n');
        res.end();
      }, 50);
    }, 50);
  },

  // Slow response (for timeout testing)
  'GET /slow': (req, res) => {
    setTimeout(() => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('slow response');
    }, 2000);
  },

  // Very slow response (should trigger timeout)
  'GET /timeout': (req, res) => {
    setTimeout(() => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('timed out response');
    }, 30000);
  },

  // Large response body
  'GET /large': (req, res) => {
    const size = parseInt(req.headers['x-requested-size'], 10) || 1048576; // 1MB default
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': String(size)
    });
    const chunk = Buffer.alloc(65536, 'A');
    let written = 0;
    const writeChunk = () => {
      while (written < size) {
        const remaining = size - written;
        const toWrite = remaining > chunk.length ? chunk : chunk.slice(0, remaining);
        if (!res.write(toWrite)) {
          written += toWrite.length;
          res.once('drain', writeChunk);
          return;
        }
        written += toWrite.length;
      }
      res.end();
    };
    writeChunk();
  },

  // JSON API endpoint
  'GET /api/data': (req, res) => {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      status: 'ok',
      timestamp: Date.now(),
      data: [1, 2, 3, 4, 5]
    }));
  },

  // POST JSON data
  'POST /api/data': (req, res) => {
    const body = [];
    req.on('data', chunk => body.push(chunk));
    req.on('end', () => {
      try {
        const data = JSON.parse(Buffer.concat(body).toString());
        res.writeHead(201, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          status: 'created',
          received: data
        }));
      } catch (e) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  },

  // PUT update
  'PUT /api/data/:id': (req, res, params) => {
    const body = [];
    req.on('data', chunk => body.push(chunk));
    req.on('end', () => {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({
        status: 'updated',
        id: params.id
      }));
    });
  },

  // DELETE
  'DELETE /api/data/:id': (req, res, params) => {
    res.writeHead(204);
    res.end();
  },

  // Error responses
  'GET /error/400': (req, res) => {
    res.writeHead(400, { 'Content-Type': 'text/plain' });
    res.end('Bad Request');
  },

  'GET /error/404': (req, res) => {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
  },

  'GET /error/500': (req, res) => {
    res.writeHead(500, { 'Content-Type': 'text/plain' });
    res.end('Internal Server Error');
  },

  'GET /error/503': (req, res) => {
    res.writeHead(503, {
      'Content-Type': 'text/plain',
      'Retry-After': '300'
    });
    res.end('Service Unavailable');
  },

  // Redirect
  'GET /redirect': (req, res) => {
    res.writeHead(302, { 'Location': '/simple' });
    res.end();
  },

  // Permanent redirect
  'GET /redirect-permanent': (req, res) => {
    res.writeHead(301, { 'Location': '/simple' });
    res.end();
  },

  // Health check
  'GET /health': (req, res) => {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'healthy' }));
  },

  // Shutdown endpoint (for test cleanup)
  'POST /shutdown': (req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('Shutting down');
    setTimeout(() => process.exit(0), 100);
  }
};

/**
 * Match a route pattern against a request
 */
function matchRoute(method, pathname) {
  const key = `${method} ${pathname}`;

  // Exact match
  if (routes[key]) {
    return { handler: routes[key], params: {} };
  }

  // Pattern match (e.g., /api/data/:id)
  for (const pattern in routes) {
    const [routeMethod, routePath] = pattern.split(' ');
    if (routeMethod !== method) continue;

    const patternParts = routePath.split('/');
    const pathParts = pathname.split('/');

    if (patternParts.length !== pathParts.length) continue;

    const params = {};
    let match = true;

    for (let i = 0; i < patternParts.length; i++) {
      if (patternParts[i].startsWith(':')) {
        params[patternParts[i].slice(1)] = pathParts[i];
      } else if (patternParts[i] !== pathParts[i]) {
        match = false;
        break;
      }
    }

    if (match) {
      return { handler: routes[pattern], params };
    }
  }

  return null;
}

/**
 * Create and start the server
 */
const server = http.createServer((req, res) => {
  const parsedUrl = url.parse(req.url, true);
  const pathname = parsedUrl.pathname;
  const method = req.method;

  console.log(`${new Date().toISOString()} ${method} ${pathname}`);

  const route = matchRoute(method, pathname);

  if (route) {
    try {
      route.handler(req, res, route.params);
    } catch (e) {
      console.error('Handler error:', e);
      res.writeHead(500, { 'Content-Type': 'text/plain' });
      res.end('Internal Server Error');
    }
  } else {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
  }
});

server.listen(port, () => {
  console.log(`Mock server listening on port ${port}`);
  console.log('Available endpoints:');
  Object.keys(routes).forEach(route => console.log(`  ${route}`));
});

// Handle shutdown gracefully
process.on('SIGTERM', () => {
  console.log('Received SIGTERM, shutting down...');
  server.close(() => process.exit(0));
});

process.on('SIGINT', () => {
  console.log('Received SIGINT, shutting down...');
  server.close(() => process.exit(0));
});
