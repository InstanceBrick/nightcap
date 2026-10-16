"""Call the local Steam controller interface for this headset setup."""
from pathlib import Path
import base64
import json
import os
import socket
import struct
import sys
import urllib.parse
import urllib.request

pages = json.load(urllib.request.urlopen('http://127.0.0.1:8080/json'))
page = next(p for p in pages if p.get('title') == 'SharedJSContext')
url = urllib.parse.urlparse(page['webSocketDebuggerUrl'])
connection = socket.create_connection((url.hostname, url.port), timeout=8)
key = base64.b64encode(os.urandom(16)).decode()
connection.sendall((f'GET {url.path} HTTP/1.1\r\nHost: {url.netloc}\r\n'
                    'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                    f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
header = b''
while not header.endswith(b'\r\n\r\n'):
    header += connection.recv(1)
assert b'101' in header.split(b'\r\n')[0]
payload = json.dumps({'id': 1, 'method': 'Runtime.evaluate', 'params': {
    'expression': Path(sys.argv[1]).read_text(), 'returnByValue': True, 'awaitPromise': True}}).encode()
mask = os.urandom(4)
length = len(payload)
header = bytes([129, 128 | length]) if length < 126 else bytes([129, 254]) + struct.pack('!H', length)
connection.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

def read(length):
    data = b''
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise EOFError('Steam connection closed')
        data += chunk
    return data

while True:
    first, second = read(2)
    length = second & 127
    if length == 126:
        length = struct.unpack('!H', read(2))[0]
    elif length == 127:
        length = struct.unpack('!Q', read(8))[0]
    mask = read(4) if second & 128 else None
    data = read(length)
    if mask:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    if first & 15 == 1:
        result = json.loads(data)
        if result.get('id') == 1:
            print(json.dumps(result))
            break
