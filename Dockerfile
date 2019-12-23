FROM debian:buster

RUN apt-get update && apt-get install -yq git build-essential pkg-config autoconf automake libtool bison flex libpq-dev parallel \
clang gcc pandoc clang-format libc++-dev libc++abi-dev cmake libstdc++6

ENV CC clang
ENV CXX clang++

ADD . /app

WORKDIR app

# RUN [ ! -d message-broker-build ] && mkdir message-broker-build

# RUN cd message-broker-build && cmake ../webrtcMessageBroker && make -j4 && cd ..

RUN ./autogen.sh && ./configure && make -j4

RUN chmod +x ./start.sh

CMD ./start.sh