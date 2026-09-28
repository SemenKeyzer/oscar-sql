#!/bin/bash
# sudo ./Prebuild_env.sh
# Build dependencies for OSCAR 2 (Qt 6) on Ubuntu 24.04 / Debian.
apt update && apt install -y build-essential \
    qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    libqt6serialport6-dev libqt6opengl6-dev libqt6sql6-sqlite qt6-connectivity-dev \
    libglu1-mesa-dev libx11-dev zlib1g-dev libudev-dev
