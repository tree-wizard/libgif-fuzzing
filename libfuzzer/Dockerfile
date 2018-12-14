#libgif afl
#Base Image is standard ubuntu build
FROM ubuntu:latest

#Install dependencies and utilities
RUN apt-get update
RUN apt-get --yes install git apt-utils
RUN apt-get --yes install libreadline-dev autoconf automake pkg-config make wget default-jdk


#Install GCC
#RUN apt-get install gcc g++

#Install Clang
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key| apt-key add -
RUN apt-get update
RUN apt-get install clang-6.0 lldb-6.0 lld-6.0 libfuzzer-6.0-dev

#Install AFL
#RUN wget "lcamtuf.coredump.cx/afl/releases/afl-latest.tgz"
#RUN tar xvfz afl-latest.tgz
#RUN cd afl-2.52b/ && make 
#RUN cd afl-2.52b/ && make install


#Download and Install giflib
RUN wget "https://gigenet.dl.sourceforge.net/project/giflib/giflib-5.1.4.tar.bz2"
RUN tar xvf giflib-5.1.4.tar.bz2


RUN cd giflib-5.1.4/ && CC=clang-6.0 CFLAGS="-g"./configure
RUN cd giflib-5.1.4/ && make check
RUN cd giflib-5.1.4/ && make install



