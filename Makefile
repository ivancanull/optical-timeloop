all:
	sudo apt-get update \
		&& sudo apt-get install -y --no-install-recommends \
						locales \
						curl \
						git \
						wget \
						python3-dev \
						python3-pip \
						scons \
						make \
						autotools-dev \
						autoconf \
						automake \
						libtool \
		&& sudo apt-get install -y --no-install-recommends \
						g++ \
						cmake

	sudo apt-get update \
		&& sudo apt-get install -y --no-install-recommends \
						g++ \
						libconfig++-dev \
						libboost-dev \
						libboost-filesystem-dev \
						libboost-iostreams-dev \
						libboost-log-dev \
						libboost-serialization-dev \
						libboost-thread-dev \
						libyaml-cpp-dev \
						libncurses5-dev \
						libtinfo-dev \
						libgpm-dev \
						libgmp-dev

	cd ntl-11.5.1\
		&& ./configure NTL_GMP_LIP=on SHARED=on \
		&& make -j8 \
		&& sudo make install

	cd barvinok-0.41.8 \
		&& ./configure --enable-shared-barvinok \
		&& make -j8 \
		&& sudo make install

	scons -j8 --with-isl --static


build:
	scons -j8 --with-isl --static
	