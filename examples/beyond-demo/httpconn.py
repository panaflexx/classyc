"""httpconn.py — one persistent HTTP/1.1 connection with lazy reconnect.

Shared by stress_client.py and stress_customers.py.  No Connection header is
sent, so HTTP/1.1 keep-alive applies; responses are framed by Content-Length
(pipelining is not used — requests are strictly sequential per connection).
"""

import asyncio

__all__ = ["HttpConn"]


class HttpConn:
    def __init__(self, host, port):
        self.host, self.port = host, port
        self.reader = None
        self.writer = None

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)

    async def close(self):
        if self.writer is not None:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass
        self.reader = self.writer = None

    async def request(self, method, path, body=b"", retries=1):
        """Send one request and read the full response.

        Reuses the open connection; on any transport error the connection is
        dropped and re-established once before giving up."""
        for attempt in range(retries + 1):
            try:
                if self.writer is None:
                    await self.connect()
                return await self._roundtrip(method, path, body)
            except Exception:
                await self.close()
                if attempt >= retries:
                    raise

    async def _roundtrip(self, method, path, body):
        req = (
            f"{method} {path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            f"Content-Length: {len(body)}\r\n"
            f"\r\n"
        ).encode()
        self.writer.write(req + body)
        await self.writer.drain()

        header = b""
        while b"\r\n\r\n" not in header:
            chunk = await self.reader.read(4096)
            if not chunk:
                raise ConnectionError("connection closed by peer")
            header += chunk

        status_line, rest = header.split(b"\r\n", 1)
        status = int(status_line.split()[1])
        content_length = 0
        for line in rest.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                content_length = int(line.split(b":", 1)[1].strip())
                break

        body_start = header.index(b"\r\n\r\n") + 4
        data = header[body_start:]
        remaining = content_length - len(data)
        while remaining > 0:
            chunk = await self.reader.read(min(remaining, 65536))
            if not chunk:
                raise ConnectionError("connection closed mid-body")
            data += chunk
            remaining -= len(chunk)
        return status, data
