struct tripstore_context;
enum TRIP_EVENT_TYPE {BEGIN, TRANSIT, END};

int open_create_db(struct tripstore_context *);
void close_db(struct tripstore_context *);

int prepare_statements(struct tripstore_context *);

int add_tripdata(struct tripstore_context *ctx,
                 int id, float lat, float lng, enum TRIP_EVENT_TYPE t,
                 int cents);

void exec_query_tofd(const char *q, struct tripstore_context *, int fd);
