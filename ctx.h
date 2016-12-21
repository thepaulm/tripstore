#include <string.h>
#include <stdlib.h>

struct tripstore_context
{
    sqlite3 *db;
    sqlite3_stmt *insert;
    sqlite3_stmt *insert_summary;
    sqlite3_stmt *update_summary;
    sqlite3_stmt *reports[3];
};

static inline struct tripstore_context *
make_ctx()
{
    struct tripstore_context * ctx = (struct tripstore_context *)
                                             malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    return ctx;
}

/* The trip messages are tiny, so we'll just make them part of the
   structure */
#define MAX_MSG_SIZE 32
struct epoll_context
{
    int fd;
    int (*cb)(struct epoll_context *, struct tripstore_context *, int);
    char msg_buf[MAX_MSG_SIZE];
    char *query_buf;
    int bytes;
};

static inline struct epoll_context *
make_epoll_ctx(int fd,
               int (*cb)(struct epoll_context *,
                         struct tripstore_context *, int))
{
    struct epoll_context *ctx = (struct epoll_context *)malloc(sizeof(*ctx));
    ctx->fd = fd;
    ctx->cb = cb;
    ctx->bytes = 0;
    ctx->query_buf = NULL;
    return ctx;
}
