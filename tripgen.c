#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "sockets.h"
#include "msgs.h"

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 8637

/* By default we'll generate lat / long in San Francisco. Note that these
   are in minute.second format. Any math on these need to be converted to
   base 10 first */
#define DEFAULT_MIN_LONG -122.30817
#define DEFAULT_MAX_LONG -122.22542
#define DEFAULT_MIN_LAT  37.42445 
#define DEFAULT_MAX_LAT  37.48479
#define DEFAULT_MIN_MINUTES 2.0
#define DEFAULT_MAX_MINUTES 10.0
#define DEFAULT_THREADS 500

#define DOLLARS_PER_MIN 4

struct options
{
    const char* host;
    int port;
    float min_long;
    float max_long;
    float min_lat;
    float max_lat;
    float min_trip_minutes;
    float max_trip_minutes;
    int threads;
};

void
syntax()
{
    printf("tripgen: generate trip data\n");
    printf("\t-h (--host): host to connect to\n");
    printf("\t-p (--port): port to connect to\n");
    printf("\t-x (--minlong): minimum longitude values\n");
    printf("\t-X (--maxlong): maximum longitude values\n");
    printf("\t-y (--minlat): minimum latitude values\n");
    printf("\t-Y (--maxlat): maximum latitude values\n");
    printf("\t-m (--minmins): minimum trip minutes\n");
    printf("\t-M (--maxmins): maximum trip minutes\n");
    printf("\t-t (--threads): how many concurrent threads\n");
    printf("\t-h (--help): this message\n");
    printf("\n");
    printf("By default, tripgen will connect to host %s on port %d,\n",
           DEFAULT_HOST, DEFAULT_PORT);
    printf("minlong %f, maxlong %f, minlat %f, maxlat %f,\n",
            DEFAULT_MIN_LONG, DEFAULT_MAX_LONG,
            DEFAULT_MIN_LAT, DEFAULT_MAX_LAT);
    printf("minmins %f, maxmins %f, and threads %d.\n",
            DEFAULT_MIN_MINUTES, DEFAULT_MAX_MINUTES, DEFAULT_THREADS);
    printf("You many omit or specify each any any of these arguments.\n");
}

int
get_options(int argc, char *a[], struct options *opts)
{
    static struct options defaults =
            {DEFAULT_HOST, DEFAULT_PORT,
             DEFAULT_MIN_LONG, DEFAULT_MAX_LONG,
             DEFAULT_MIN_LAT, DEFAULT_MAX_LAT,
             DEFAULT_MIN_MINUTES, DEFAULT_MAX_MINUTES,
             DEFAULT_THREADS};
    static struct option long_options[] = {
        {"host", required_argument, 0, 'H'},
        {"port", required_argument, 0, 'p'},
        {"minlong", required_argument, 0, 'x'},
        {"maxlong", required_argument, 0, 'X'},
        {"minlat", required_argument, 0, 'y'},
        {"maxlat", required_argument, 0, 'Y'},
        {"minmins", required_argument, 0, 'm'},
        {"maxmins", required_argument, 0, 'M'},
        {"threads", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    *opts = defaults;

    int c;
    int option_index;
    while (1) {
        c = getopt_long(argc, a, "H:p:x:X:y:Y:m:M:t:h", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'H':
                opts->host = optarg;
                break;
            case 'p':
                opts->port = atoi(optarg);
                break;
            case 'x':
                opts->min_long = atof(optarg);
                break;
            case 'X':
                opts->max_long = atof(optarg);
                break;
            case 'y':
                opts->min_lat = atof(optarg);
                break;
            case 'Y':
                opts->max_lat = atof(optarg);
                break;
            case 'm':
                opts->min_trip_minutes = atof(optarg);
                break;
            case 'M':
                opts->max_trip_minutes = atof(optarg);
                break;
            case 't':
                opts->threads = atoi(optarg);
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

/* generate lat / long based on command line parameters */
void
generate_long_lat(struct options *opts, float *lng, float *lat)
{
    *lng = opts->min_long + (opts->max_long - opts->min_long) *
           (float)rand() / (float)RAND_MAX;
    *lat = opts->min_lat + (opts->max_lat - opts->min_lat) *
           (float)rand() / (float)RAND_MAX;
}

/* generate a seconds value based on command line parameters */
int
generate_trip_seconds(struct options *opts)
{
    int max_seconds = opts->max_trip_minutes * 60;
    int min_seconds = opts->min_trip_minutes * 60;

    return min_seconds + (max_seconds - min_seconds) *
           (float)rand() / (float)RAND_MAX;
}

/* run_client: this is the main client loop */
void *
run_client(void *arg)
{
    printf("run client starting ...\n");
    float lng, lat;
    struct options *opts = (struct options *)arg;
    srand(time(NULL));

    /* connect to tripstore */
    int s = sock_connect(opts->host, opts->port);
    if (s < 0) {
        fprintf(stderr, "unable to connect to %s:%d\n", opts->host, opts->port);
        return (void*)-1;
    }

    while (1) { // run forever
        /* calculate how long this trip is going to take and how
           much it's going to cost */
        int seconds = generate_trip_seconds(opts);
        int fare_cents = (seconds / 60.0) * (DOLLARS_PER_MIN * 100.0);

        /* generate our lat/long based on the command line options */
        generate_long_lat(opts, &lng, &lat);

        /* Begin message goes out and then we read our assigned trip id */
        send_begin_msg(s, lng, lat);
        int id = recv_trip_id(s);

        /* for each one second update ... */
        while (seconds--) {
            /* generate new long / lat */
            generate_long_lat(opts, &lng, &lat);
            /* and update tripstore */
            if (-1 == send_update_msg(s, id, lng, lat)) {
                printf("Connection closed.\n");
                return (void*)-1;
            }
            sleep(1);
        }
        /* generate one last long/lat and send END message with fare */
        generate_long_lat(opts, &lng, &lat);
        send_end_msg(s, id, lng, lat, fare_cents);
    }
    return (void*)0;
}

/* main just gets the command line parameters and spins up our threads
   for us */
int
main(int argc, char *argv[])
{
    struct options opts;
    if (get_options(argc, argv, &opts) < 0)
        return -1;

    printf("tripgen starting with %d threads.\n", opts.threads);
    int t;
    pthread_t thr;
    for (t = 0; t < opts.threads; t++) {
        if (0 != pthread_create(&thr, NULL, run_client, (void*)&opts))
            fprintf(stderr, "Failed to create thread %d\n", t);
    }

    while (1)
        sleep(60);

    return 0;
}
