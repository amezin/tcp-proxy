import time

from . import util


def test_simple(subprocess_launcher, proxy_launcher):
    iperf_server = subprocess_launcher(['iperf3', '--server', '--one-off', '--bind', '127.0.0.1'])
    server_addr = util.wait_tcp_listen(iperf_server)

    proxy = proxy_launcher(target_host=server_addr[0], target_port=server_addr[1])
    proxy_addr = util.wait_tcp_listen(proxy)

    iperf_client = subprocess_launcher(['iperf3', '--client', proxy_addr[0], '--port', str(proxy_addr[1]), '--parallel', '16', '--bidir'])

    assert iperf_client.wait() == 0
    assert iperf_server.wait() == 0

    proxy.terminate()
