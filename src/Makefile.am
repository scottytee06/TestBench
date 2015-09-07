include $(top_srcdir)/common.am

bin_PROGRAMS = modbus_server

modbus_server_SOURCES = modbus_server.c
modbus_server_CFLAGS = $(AM_CFLAGS) $(MODBUS_CFLAGS)
modbus_server_LDADD = $(MODBUS_LIBS)
