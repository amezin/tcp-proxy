import concurrent.futures
import contextlib
import logging
import random
import socket
import subprocess
import time

import psutil
import pytest


RAND = random.Random(0)
TEST_BLOB = RAND.randbytes(20000000)
MIN_CHUNK = 1
MAX_CHUNK = 8192

LOG = logging.getLogger(__name__)
LOG.setLevel(logging.INFO)


def shutdown_process(popen):
    try:
        popen.wait(timeout=0.2)
    except psutil.TimeoutExpired:
        popen.terminate()

        try:
            popen.wait(timeout=0.2)
        except psutil.TimeoutExpired:
            popen.kill()
            raise


def wait_tcp_listen(popen, port=None):
    proc = psutil.Process(popen.pid)
    while popen.poll() == None:
        for conn in proc.connections('tcp'):
            if conn.status != psutil.CONN_LISTEN:
                continue

            if (port is None) or (conn.laddr[1] == port):
                return conn.laddr

        time.sleep(0.01)

    pytest.fail(f'Process {popen.pid} is not running')


def recvall(sock, rand, maxlen=None, minrecv=MIN_CHUNK, maxrecv=MAX_CHUNK):
    data = b''

    while True:
        if maxlen:
            if len(data) == maxlen:
                break

            maxrecv = min(maxrecv, maxlen - len(data))
            minrecv = min(minrecv, maxlen - len(data))

        recvd = sock.recv(rand.randint(minrecv, maxrecv))
        if not recvd:
            break

        data += recvd

    return data


def sendall(sock, data, rand, minsend=MIN_CHUNK, maxsend=MAX_CHUNK):
    data = data[:]
    while data:
        minsend = min(minsend, len(data))
        maxsend = max(maxsend, len(data))
        send_size = rand.randint(minsend, maxsend)
        n_sent = sock.send(data[:send_size])
        data = data[n_sent:]


@pytest.fixture
def server_socket():
    with socket.create_server(('127.0.0.1', 0)) as server_socket:
        server_socket.settimeout(10)
        yield server_socket


@pytest.fixture
def threadpool():
    with concurrent.futures.ThreadPoolExecutor() as executor:
        yield executor.submit


@pytest.fixture
def subprocess_launcher():
    with contextlib.ExitStack() as stack:

        def launch(*args, **kwargs):
            popen = psutil.Popen(*args, **kwargs)
            stack.callback(shutdown_process, popen)
            return popen

        yield launch


@pytest.fixture
def proxy_launcher(subprocess_launcher):
    def launch(*, proxy_host='localhost', proxy_port=0, target_host='localhost', target_port):
        return subprocess_launcher(['proxy', proxy_host, str(proxy_port), target_host, str(target_port)])

    return launch


@pytest.mark.parametrize('data', [b'testdata', b'', pytest.param(TEST_BLOB, id='blob')])
def test_client_send_server_recv(data, proxy_launcher, server_socket, threadpool):
    server_addr = server_socket.getsockname()
    rand = random.Random(1)

    def server():
        client_socket, _ = server_socket.accept()
        client_socket.settimeout(10)
        with client_socket:
            return recvall(client_socket, rand)

    server_fut = threadpool(server)

    proxy = proxy_launcher(target_host=server_addr[0], target_port=server_addr[1])
    proxy_addr = wait_tcp_listen(proxy)

    with socket.create_connection(proxy_addr, timeout=10) as client_socket:
        sendall(client_socket, data, rand)

    assert server_fut.result() == data
    assert proxy.wait() == 0


@pytest.mark.parametrize('data', [b'testdata', b'', pytest.param(TEST_BLOB, id='blob')])
def test_client_recv_server_send(data, proxy_launcher, server_socket, threadpool):
    server_addr = server_socket.getsockname()
    rand = random.Random(2)

    def server():
        client_socket, _ = server_socket.accept()
        client_socket.settimeout(10)
        with client_socket:
            sendall(client_socket, data, rand)

    server_fut = threadpool(server)

    proxy = proxy_launcher(target_host=server_addr[0], target_port=server_addr[1])
    proxy_addr = wait_tcp_listen(proxy)

    with socket.create_connection(proxy_addr, timeout=10) as client_socket:
        assert recvall(client_socket, rand) == data

    server_fut.result()
    assert proxy.wait() == 0


def test_echo_server(proxy_launcher, server_socket, threadpool):
    server_addr = server_socket.getsockname()
    rand = random.Random(2)

    data = [
        b'testdata',
        b'm',
        b'biiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiigmsg',
        b'z' * MAX_CHUNK
    ]

    def server():
        client_socket, _ = server_socket.accept()
        client_socket.settimeout(10)
        with client_socket:
            for msg in data:
                recvd = recvall(client_socket, rand, len(msg))
                assert recvd == msg
                sendall(client_socket, recvd, rand)

    server_fut = threadpool(server)

    proxy = proxy_launcher(target_host=server_addr[0], target_port=server_addr[1])
    proxy_addr = wait_tcp_listen(proxy)

    with socket.create_connection(proxy_addr, timeout=10) as client_socket:
        for msg in data:
            sendall(client_socket, msg, rand)
            assert recvall(client_socket, rand, len(msg)) == msg

    server_fut.result()
    assert proxy.wait() == 0
