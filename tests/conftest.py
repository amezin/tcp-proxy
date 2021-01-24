import contextlib

import psutil
import pytest

from . import util


@pytest.fixture
def subprocess_launcher():
    with contextlib.ExitStack() as stack:

        def launch(*args, **kwargs):
            popen = psutil.Popen(*args, **kwargs)
            stack.callback(util.shutdown_process, popen)
            return popen

        yield launch


@pytest.fixture
def proxy_launcher(subprocess_launcher):
    def launch(*, proxy_host='localhost', proxy_port=0, target_host='localhost', target_port):
        return subprocess_launcher(['proxy', proxy_host, str(proxy_port), target_host, str(target_port)])

    return launch
