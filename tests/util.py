import psutil
import pytest

import time


def shutdown_process(popen):
    try:
        assert popen.wait(timeout=0.2) == 0
    except psutil.TimeoutExpired:
        popen.terminate()

        try:
            assert popen.wait(timeout=0.2) == 0
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
