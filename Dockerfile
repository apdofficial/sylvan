FROM aflplusplus/aflplusplus:latest

# this is for timezone config
RUN apt-get update && apt-get install -y locales && rm -rf /var/lib/apt/lists/* \
	&& localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG en_US.utf8

# system packages
RUN apt-get update
RUN apt-get install -y apt-utils

# essential
RUN apt-get install -y curl
RUN apt-get install -y build-essential git openssh-server pkg-config manpages-dev systemd-coredump htop
RUN apt-get install -y build-essential
RUN apt-get install -y libboost-all-dev

# Install C/ C++ related dependencies
RUN apt-get install -y cmake gdb clang lldb lld
# Static and Dynamic anaylsis tools 
RUN apt-get install -y cppcheck
RUN apt-get install -y valgrind 

# Address sanitizers:
# https://www.usenix.org/system/files/conference/atc12/atc12-final39.pdf
# https://github.com/google/sanitizers/wiki/AddressSanitizer
# - Use after free (dangling pointer reference)
# - Heap buffer overflow
# - Stack buffer overflow
# - Use after return
# - Use after scope
# - Initialization order bugs

# Memory sanitizers:
# https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/43308.pdf
# https://github.com/google/sanitizers/wiki/MemorySanitizer 
# It can track uninitialized bits in a bitfield. It will tolerate copying of uninitialized memory, 
# and also simple logic and arithmetic operations with it. In general, 
# MSan silently tracks the spread of uninitialized data in memory, and reports a warning when a code 
# branch is taken (or not taken) depending on an uninitialized value.

ENV HOME=/home
ENV SYLVAN=$HOME/sylvan
ENV SYLVAN_MODELS=$SYLVAN/models

RUN cd $HOME && mkdir sylvan

COPY . $SYLVAN
WORKDIR $SYLVAN

RUN mkdir build
RUN cd build && cmake ..
RUN cd build && make