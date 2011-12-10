LIB_HOME = $(shell pwd)/source

all:	keys_test config_test
	$(MAKE) -C source PROGRAMS="dropbear"

keys_test:
	( if [ -f ./keys_done ]; then \
		echo "the same keys"; \
	else \
		echo "Generating keys"; \
		dropbearkey -t rsa -f $(LIB_HOME)/../dropbear_rsa_host_key ; \
		rm -f keys_done; \
		touch keys_done; \
	fi )

config_test:
	( if [ -f ./config_done ]; then \
		echo "the same configuration"; \
	else \
		make configure; \
		rm -f config_done; \
		touch config_done; \
	fi ) 

configure:
	( cd source ; \
	./configure \
		--host=mipsel-linux \
		--prefix=$(LIB_HOME)/../install \
		--disable-lastlog \
		--disable-wtmp \
	)

clean:
	$(MAKE) -C source distclean
	rm -rf install
	rm -f config_done
	rm -f keys_done

romfs:
#	echo "do nothing"
	cp source/dropbear .
	$(STRIP) dropbear
	$(ROMFSINST) /sbin/dropbear
	$(ROMFSINST) -d /etc_ro/dropbear/dropbear_rsa_host_key

