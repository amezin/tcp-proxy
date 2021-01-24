FROM fedora:32 AS proxy

RUN dnf install -y gcc-c++ cmake ninja-build

COPY . /usr/src/proxy

RUN mkdir -p /usr/src/proxy/build && \
    cd /usr/src/proxy/build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. && \
    ninja install

ENTRYPOINT ["/usr/local/bin/proxy"]

FROM proxy AS tox

RUN dnf install -y --setopt=install_weak_deps=False tox python39 iperf3

WORKDIR /usr/src/proxy/build
ENTRYPOINT ["tox"]
