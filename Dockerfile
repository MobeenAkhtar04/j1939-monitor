FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
      build-essential cmake libgtest-dev qt6-base-dev qt6-qpa-plugins \
      socat can-utils iproute2 kmod sudo \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
