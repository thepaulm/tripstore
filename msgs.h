/* This is the general messaging interface. Both tripgen and tripstore utilize
   these. See the .c files for more description */
enum MSG_TYPE {MSG_BEGIN, MSG_ID, MSG_UPDATE, MSG_END};

int send_begin_msg(int s, float lng, float lat);
int send_update_msg(int s, int id, float lng, float lat);
int send_end_msg(int s, int id, float lng, float lat, int cents);
int parse_msg(char *buf, int size,
              enum MSG_TYPE *t, int *id, float *lng, float *lat, int *cents);

int send_trip_id(int s, int id);
int recv_trip_id(int s);
