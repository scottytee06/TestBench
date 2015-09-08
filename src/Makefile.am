include $(top_srcdir)/common.am

bin_PROGRAMS = modbus_server bacnet_client

modbus_server_SOURCES = modbus_server.c
modbus_server_CFLAGS = $(AM_CFLAGS) $(MODBUS_CFLAGS)
modbus_server_LDADD = $(AM_LIBS) $(MODBUS_LIBS)

BACNET_DEPENDENT_SRC = bacnet_client.c
bacnet_client_SOURCES = $(BACNET_DEPENDENT_SRC)
bacnet_client_CFLAGS = $(AM_CFLAGS) $(BACNET_CFLAGS)
bacnet_client_LDADD = $(AM_LIBS) $(BACNET_LIBS)

BUILT_SOURCES = bacnet_namespace.h .bacnet_dependent_src_stamp

EXTRA_DIST = bacnet_api_names

.bacnet_dependent_src_stamp: bacnet_api_names $(BACNET_DEPENDENT_SRC)
	cat $< | $(top_srcdir)/scripts/check_namespace.sh $< $?
	touch $@

bacnet_namespace.h: bacnet_api_names
	cat $< | $(top_srcdir)/scripts/build_namespace.sh > $@
