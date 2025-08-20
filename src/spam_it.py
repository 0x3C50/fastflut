import socket

socks = []

for e in range(4 * 16):
    ts = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    socks.append(ts)
    ts.connect(("127.0.0.1", 1234))

while True:
    for sock in socks:
        sock.sendall(b"a" * 32)
