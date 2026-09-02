# Development and test image for the Quake III Arena modern fork.
#
# The image holds every build and test dependency so that nothing is installed on the host.
# Mesa llvmpipe provides software OpenGL and lavapipe provides software Vulkan, so the
# renderer gates run headless under Xvfb. The golden images in ci/smoke/golden/ depend on the
# Mesa version, so pin the base image by digest and record the Mesa version when you refresh it.
#
# Refresh the pin:
#   docker pull ubuntu:24.04
#   docker inspect --format '{{index .RepoDigests 0}}' ubuntu:24.04
# Then rebuild, run `make shell` and `glxinfo -B`, and update the pin comment below.

# Pinned 2 September 2026. Mesa at this pin: 4.5 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2
FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang gdb ccache cmake ninja-build pkg-config git ca-certificates \
        clang-format clang-tidy python3 file \
        libsdl2-dev libgl-dev libx11-dev libxext-dev \
        libluajit-5.1-dev libcurl4-openssl-dev libgtest-dev \
        libvulkan-dev vulkan-tools \
        mesa-utils libgl1-mesa-dri mesa-vulkan-drivers xvfb \
        apitrace imagemagick \
    && rm -rf /var/lib/apt/lists/*

# The compose file runs the container as the host user so that build output on the bind mount
# belongs to you. That user has no passwd entry, so give it a writable HOME and a ccache
# directory. A named volume mounted at /ccache inherits these permissions on first use.
RUN mkdir -p /tmp/q3home /ccache \
    && chmod 1777 /tmp/q3home /ccache \
    && printf '[safe]\n\tdirectory = /src\n' > /tmp/q3home/.gitconfig \
    && chmod 644 /tmp/q3home/.gitconfig

ENV HOME=/tmp/q3home \
    CCACHE_DIR=/ccache \
    LIBGL_ALWAYS_SOFTWARE=1 \
    GALLIUM_DRIVER=llvmpipe \
    SDL_AUDIODRIVER=dummy

WORKDIR /src
