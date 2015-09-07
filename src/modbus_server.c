#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <modbus-tcp.h>

#define NUM_REGS 3
#define LISTEN_BACKLOG 1

/* Hack:
 * libmodbus doesn't expose the context structure which is a problem for a
 * multithreaded server, as read/write operations rely on a file descriptor that
 * is written to by modbus_tcp_accept(). If multiple connections are created,
 * the file descriptor will be written over. Define a structure here to allow
 * sizeof() to determine how large modbus_t is */
struct ctx_data {
    int slave;
    int s; /* Modified by modbus_tcp_accept() */
    int debug;
    int error_recovery;
    struct timeval response_timeout;
    struct timeval byte_timeout;
    const void *backend;
    void *backend_data;
};

static modbus_mapping_t *mb_mapping;

static void *connection(void *arg) {
    modbus_t *ctx = (modbus_t *) arg;
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    int bytes;

    while (1) {
	bytes = modbus_receive(ctx, query);
	if (bytes > 0) {
	    modbus_reply(ctx, query, bytes, mb_mapping);
	}

	/* Other end has disconnected */
	if (bytes == -1) break;
    }

    free(ctx);
    return arg;
}

static void *update_regs(void *arg) {
    while (1) {
	mb_mapping->tab_registers[0] = rand();
	mb_mapping->tab_registers[1] = rand();
	mb_mapping->tab_registers[2] = rand();
	sleep(1);
    }
    return arg;
}

int main(int argc, char *arg[]) {
    modbus_t *ctx, *dup_ctx;
    int server_fd;
    pthread_t tcp_thread;
    pthread_t data_thread;

    if ((ctx = modbus_new_tcp("SERVER", MODBUS_TCP_DEFAULT_PORT)) == NULL) {
	printf("Failied to initialise modbus\n");
	return -1;
    }

    mb_mapping = modbus_mapping_new(0, 0, NUM_REGS, 0);
    pthread_create(&data_thread, 0, update_regs, NULL);

listen:
    if ((server_fd = modbus_tcp_listen(ctx, LISTEN_BACKLOG)) < 0) {
	printf("Failed to initiate modbus_tcp server, %i\n", server_fd);
	modbus_free(ctx);
	return -1;
    }

    while (1) {
	modbus_set_debug(ctx, TRUE);
	if (modbus_tcp_accept(ctx, &server_fd) < 0) {
	    /* libmodbus closes the supplied socket if accept fails - this may
	     * be making new connections impossible */
	    printf("Accept failed \n");
	    goto listen;
	}
	modbus_set_debug(ctx, FALSE);
	dup_ctx = malloc(sizeof(struct ctx_data));
	memcpy(dup_ctx, ctx, sizeof(struct ctx_data));
	pthread_create(&tcp_thread, 0, connection, dup_ctx);
    }

    modbus_mapping_free(mb_mapping);
    close(server_fd);
    modbus_free(ctx);

    return 0;
}
