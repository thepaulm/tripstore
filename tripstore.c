#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/epoll.h>
#include "sqlite3.h"
#include "sqls.h"
#include "ctx.h"
#include "msgs.h"

#define GENPORT 8637
#define QUERYPORT 8638

#define EPOLL_EVENTS 256

/* This is the global allocator for trip ids */
static int next_trip_id = 1;

struct options
{
    int port;
    int query_port;
};

void
syntax()
{
    printf("tripstore: store trip data in memory\n");
    printf("\t-p (--port): port to listen on for tripgen\n");
    printf("\t-q (--query-port): port to listen on for queries\n");
    printf("\t-h (--help): this message\n");
    printf("By default tripstore will listen on %d for tripgen and "
           "%d for queries.\n", GENPORT, QUERYPORT);
}

/* Helper functions */
int
get_options(int argc, char *a[], struct options *opts)
{
    static struct options defaults = {GENPORT, QUERYPORT};
    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"query-port", required_argument, 0, 'q'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    *opts = defaults;

    int c;
    int option_index;
    while (1) {
        c = getopt_long(argc, a, "p:q:h", long_options, &option_index);
        
        if (c == -1)
            break;

        switch (c) {
            case 'p':
                opts->port = atoi(optarg);
                break;
            case 'q':
                opts->query_port = atoi(optarg);
                break;
            case 'h':
                syntax();
                exit(0);
                break;

            default:
                fprintf(stderr, "bad parameter at: %s\n",
                        long_options[option_index].name);
                break;
        }
    }
    return 0;

}

/* Allocate a new trip id and notify the requester */
int
allocate_send_id(int s)
{
    int id = next_trip_id++;
    send_trip_id(s, id);
    return id;
}

void
cleanup_epc(int efd, struct epoll_context *epc)
{
    epoll_ctl(efd, EPOLL_CTL_DEL, epc->fd, NULL);
    close(epc->fd);
    if (epc->query_buf)
        free(epc->query_buf);
    free(epc);
}

/* Parse incoming messages from the trip generator and to database
   actions related to the incoming data */
int
handle_msg(char *data, int size, int s, struct tripstore_context *ctx)
{
    enum MSG_TYPE t;
    int id;
    float lng, lat;
    int cents;

    if (-1 == parse_msg(data, size, &t, &id, &lng, &lat, &cents)) {
        return -1;
    }

    switch (t) {
        case MSG_BEGIN:
            id = allocate_send_id(s);
            add_tripdata(ctx, id, lng, lat, BEGIN, 0);
            break;

        case MSG_UPDATE:
            add_tripdata(ctx, id, lng, lat, TRANSIT, 0);
            break;

        case MSG_END:
            add_tripdata(ctx, id, lng, lat, END, cents);
            break;

        default:
            fprintf(stderr, "Got unown msg\n");
    }
    return 0;
}

/* handle_read: receiving data from the trip generator */
int
handle_read(struct epoll_context *epc, struct tripstore_context *ctx, int efd)
{
    int x = read(epc->fd, epc->msg_buf + epc->bytes, MAX_MSG_SIZE - epc->bytes);

    if (x <= 0) {
        cleanup_epc(efd, epc);
        return 0;
    }

    epc->bytes += x;

    /* loop through the data in the buffer until we don't have a full
       message */
    while (1) {
        if (epc->bytes < sizeof(uint16_t))
            return 0;

        uint16_t size;
        size = *(uint16_t*)epc->msg_buf;    
        if (epc->bytes < size)
            return 0;

        if (-1 == handle_msg(epc->msg_buf, size, epc->fd, ctx))
            return -1;
               
        memmove(epc->msg_buf, epc->msg_buf + size, epc->bytes - size);
        epc->bytes -= size;
    }
}

/* handle_query: receiving data from the query interface */
#define QUERY_BUF_SIZE 2048
int
handle_query(struct epoll_context *epc, struct tripstore_context *ctx, int efd)
{
    /* The epoll_context for the query interface needs it's own special
       bigger buffer */
    if (!epc->query_buf) {
        epc->query_buf = (char *)malloc(QUERY_BUF_SIZE);
    } 
    int x = read(epc->fd, epc->query_buf + epc->bytes,
                 QUERY_BUF_SIZE - epc->bytes);
    if (x <= 0) {
        cleanup_epc(efd, epc);
        return 0;
    }

    int ran_one = 1;
    epc->bytes += x;

    /* Loop through the data in the query buffer until we don't have a
       full null terminated line */
    while (epc->bytes && ran_one) {
        ran_one = 0;
        int i;
        char *query;
        for (i = 0; i < epc->bytes; i++) {
            if (epc->query_buf[i] == '\n') {
                /* Copy query to new buffer so we can 0 terminate */
                query = (char *)malloc(i + 1);
                memcpy(query, epc->query_buf, i);
                query[i] = 0;
                /* Run ad-hoc query to the output fd */
                exec_query_tofd(query, ctx, epc->fd);
                free(query);

                /* Copy back the rest of the bytes that were in the input
                   buffer and run again */
                i++;
                memmove(epc->query_buf, epc->query_buf + i, epc->bytes - i);
                epc->bytes -= i;
                ran_one = 1;
                break;
            }
        }
    }
    return 0;
}

/* handle_accept: genereic acceptor closure for sockets */
int
handle_accept(struct epoll_context *epc, struct tripstore_context *ctx, int efd,
              int (*cb)(struct epoll_context *,
                        struct tripstore_context *,
                        int))
{
    int s = accept(epc->fd, NULL, 0);
    if (s > 0) {
        struct epoll_event evt;
        evt.events = EPOLLIN;
        evt.data.ptr = make_epoll_ctx(s, cb);
        if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, s, &evt)) {
            fprintf(stderr, "acceptor could not register reads\n");
            close(s);
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

/* handle_gen_accept: accept callback on the generator socket */
int
handle_gen_accept(struct epoll_context *epc, struct tripstore_context *ctx,
                  int efd)
{
    return handle_accept(epc, ctx, efd, handle_read);
}

/* handle_query_accept: accept callback on the query socket */
int
handle_query_accept(struct epoll_context *epc, struct tripstore_context *ctx,
                    int efd)
{
    return handle_accept(epc, ctx, efd, handle_query);
}


int
main(int argc, char *argv[])
{
    struct options opts;
    if (get_options(argc, argv, &opts) < 0)
            return -1;

    printf("listening on port %d for gen, %d for queries.\n",
            opts.port, opts.query_port);
    struct tripstore_context * ctx = make_ctx();

    /* Make our initial database from the ddl and connect */
    if (open_create_db(ctx) < 0) {
        fprintf(stderr, "open_create_db failed.\n");
        return -1;
    }

    /* Create prepared statements for our inserts / updates / and reports */
    if (prepare_statements(ctx) < 0) {
        fprintf(stderr, "prepare_statements failed.\n");
        return -1;
    }

    /* Open up our ports and create the epoll */
    int s = listen_on_port(opts.port);
    int q = listen_on_port(opts.query_port);
    int efd = epoll_create1(0);

    if (s < 0 || q < 0 || efd < 0) {
        fprintf(stderr, "error in socketing\n");
        return -1;
    }

    /* Add the generator socket and the query socket to the epoll */
    struct epoll_event evt;
    evt.events = EPOLLIN;
    evt.data.ptr = make_epoll_ctx(s, handle_gen_accept);
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, s, &evt)) {
        fprintf(stderr, "Failed to epoll_ctl\n");
        return -1;
    }
    evt.events = EPOLLIN;
    evt.data.ptr = make_epoll_ctx(q, handle_query_accept);
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, q, &evt)) {
        fprintf(stderr, "Failed to epoll_ctl for queries\n");
    }

    /* This is the main event loop. We epoll on our sockets and
       run the associated callbacks for read events */
    struct epoll_event events[EPOLL_EVENTS];
    while (1) { 
        int x = epoll_wait(efd, events, EPOLL_EVENTS, -1);
        if (x > 0) {
            int i;
            for (i = 0; i < x; i++) {
                struct epoll_context *epc = (struct epoll_context *)
                                            events[i].data.ptr;
                epc->cb(epc, ctx, efd);
            }
        }
    }

    close(efd);
    close(s);
    close_db(ctx);
    free(ctx);
    return 0;
}
