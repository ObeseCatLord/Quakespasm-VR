# Ubuntu 22.04 (glibc 2.35) keeps the shipped runtime usable on 22.04, Debian 12
# and later hosts; host-policy.json enforces the matching GLIBC ceiling.
FROM ubuntu:22.04@sha256:9abcaffc92a5279fff3a6a41635c0800ea33e528425fb389658806476e7defde

COPY sources.json /usr/local/bin/sources.json
COPY build-native.sh /usr/local/bin/build-native.sh
COPY Dockerfile /usr/local/bin/Dockerfile
COPY package.py host-policy.json /usr/local/bin/
ENV DEBIAN_FRONTEND=noninteractive

RUN set -eu; \
    sed -i 's/^# deb-src /deb-src /' /etc/apt/sources.list; \
    test "$(grep -c '^deb ' /etc/apt/sources.list)" -gt 0; \
    test "$(grep -c '^deb ' /etc/apt/sources.list)" = "$(grep -c '^deb-src ' /etc/apt/sources.list)"; \
    apt-get update; \
    # Bring base-image packages up to the current index: package.py re-downloads
    # the exact .deb of every shipped package, and superseded versions that are
    # only in the base image are no longer listed.
    apt-get -y upgrade; \
    apt-get install -y --no-install-recommends python3 ca-certificates

RUN set -eu; \
    packages="$(python3 -c 'import json; print(" ".join(json.load(open("/usr/local/bin/sources.json"))["apt_packages"]))')"; \
    apt-get install -y --no-install-recommends $packages

ENTRYPOINT ["/usr/local/bin/build-native.sh"]
