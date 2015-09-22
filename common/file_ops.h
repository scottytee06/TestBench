#define SERVER_RANDOM_DATA_LENGTH 128

#define RANDOM_DATA_POOL "random_data"

extern void file_read_random_data(const char *prefix);
extern uint16_t file_get_random_data(int device_id, int channel_id);
extern int file_get_highest_channel(void);
extern void file_print_random_data();
extern void file_update_regs(uint16_t *regs, int device_id);
extern void file_free_random_data();
