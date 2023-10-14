#ifndef UID_H_
#define UID_H_

#define UID_UNIQUE_ID_STRING_SIZE	32

int uid_generate_unique_id(char *uid_buf, size_t uid_buf_size,
			   const char *part_number,
			   const char *sensor_name,
			   const char *serial_number);
char * uid_get_device_id(void);
int uid_init(void);

#endif /* UID_H_ */