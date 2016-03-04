FROM library/ubuntu:15.10
MAINTAINER "Martin Aumueller" <aumueller@hlrs.de>

ENV PAR -j8
ENV ISPCVER 1.8.2
ENV EMBREETAG v2.8.0
ENV ARCHSUFFIX werewolfopt
ENV BUILDTYPE Release
ENV PREFIX /usr
ENV BUILDDIR /build
WORKDIR ${BUILDDIR}
ADD embree-debian-multiarch.diff ${BUILDDIR}/embree-debian-multiarch.diff

RUN apt-get update -y && apt-get install -y \
       libtbb-dev libglew-dev \
       qttools5-dev qtscript5-dev libqt5scripttools5 libqt5svg5-dev libqt5opengl5-dev libqt5webkit5-dev \
       libmpich-dev mpich \
       git cmake make file \
       libopenscenegraph-dev libjpeg-dev \
       libvncserver-dev \
       libsnappy-dev zlib1g-dev libreadline-dev \
       libxmu-dev libxi-dev \
       libboost-atomic-dev libboost-chrono-dev libboost-date-time-dev libboost-exception-dev libboost-filesystem-dev \
       libboost-iostreams-dev libboost-locale-dev libboost-log-dev libboost-math-dev libboost-program-options-dev libboost-python-dev \
       libboost-random-dev libboost-regex-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev libboost-timer-dev \
       libboost-tools-dev libboost-dev

# https://bugs.launchpad.net/ubuntu/+source/libjpeg-turbo/+bug/1369067
RUN apt-get install -y libturbojpeg && cd /usr/lib/x86_64-linux-gnu && ln -s libturbojpeg.so.0 libturbojpeg.so

# install ispc - prerequisite for embree
RUN apt-get install -y wget \
       && cd /tmp \
       && wget http://sourceforge.net/projects/ispcmirror/files/v${ISPCVER}/ispc-v${ISPCVER}-linux.tar.gz/download \
       && tar -C /usr/bin -x -f /tmp/download --strip-components=1 ispc-v${ISPCVER}-linux/ispc \
       && rm download

# build embree CPU ray tracer
RUN git clone git://github.com/embree/embree.git && cd embree && git checkout ${EMBREETAG} \
      && git apply ../embree-debian-multiarch.diff \
      && rm ../embree-debian-multiarch.diff \
      && mkdir build && cd build \
      && cmake -DCMAKE_BUILD_TYPE=${BUILDTYPE} -DCMAKE_INSTALL_PREFIX=${PREFIX} -DENABLE_TUTORIALS=OFF .. \
      && make ${PAR} install \
      && cd ${BUILDDIR} \
      && rm -rf embree

# build COVISE file I/O library
RUN git clone git://github.com/hlrs-vis/covise.git \
       && export ARCHSUFFIX=${ARCHSUFFIX} \
       && export COVISEDIR=${BUILDDIR}/covise \
       && cd ${BUILDDIR}/covise \
       && mkdir -p build.covise \
       && cd build.covise \
       && cmake .. -DCOVISE_BUILD_ONLY_FILE=TRUE -DCMAKE_INSTALL_PREFIX=${PREFIX} -DCMAKE_BUILD_TYPE=${BUILDTYPE} -DCOVISE_WARNING_IS_ERROR=FALSE \
       && make ${PAR} install \
       && cd ${BUILDDIR} \
       && rm -rf covise

# Vistle proper
EXPOSE 31093 31590
RUN git clone --recursive git://github.com/vistle/vistle.git \
       && export COVISEDESTDIR=${BUILDDIR}/covise \
       && cd ${BUILDDIR}/vistle \
       && mkdir build.vistle \
       && cd build.vistle \
       && cmake -DCMAKE_INSTALL_PREFIX=${PREFIX} -DENABLE_INSTALLER=FALSE -DCMAKE_BUILD_TYPE=${BUILDTYPE} .. \
       && make ${PAR} install \
       && cd ${BUILDDIR} \
       && rm -rf vistle

ENTRYPOINT ["/usr/bin/vistle"]
CMD ["-b"]