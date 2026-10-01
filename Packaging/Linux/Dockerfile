FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3

COPY sources.json /usr/local/bin/sources.json
COPY build-native.sh /usr/local/bin/build-native.sh
COPY Dockerfile /usr/local/bin/Dockerfile
COPY package.py host-policy.json /usr/local/bin/
ENV DEBIAN_FRONTEND=noninteractive

RUN set -eu; \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources; \
    grep -q '^Types: deb deb-src$' /etc/apt/sources.list.d/ubuntu.sources; \
    apt-get update; \
    apt-get install -y --no-install-recommends python3 ca-certificates

RUN set -eu; \
    packages="$(python3 -c 'import json; print(" ".join(json.load(open("/usr/local/bin/sources.json"))["apt_packages"]))')"; \
    apt-get install -y --no-install-recommends $packages

ENTRYPOINT ["/usr/local/bin/build-native.sh"]
