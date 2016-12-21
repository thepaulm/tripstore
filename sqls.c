#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "sqls.h"
#include "ctx.h"

/*

   We have two tables:

    triplog: 

     id INTEGER | long REAL | lat REAL | type INTEGER | face_cents INTEGER

             Log of each of the begin, update, and end messages. This is
             a denormalized log. fares are stored as cents (because this
             is money and using a floating point number for dollars and
             cents leads to round off errors. long at lat are stored as
             REAL, but this is actually a floating point number. These are
             stored in minute.second format, so if we ever need to do
             math we have to convert these to base 10 first.


    tripsummary: 

    id INTEGER | begin INTEGER | end INTEGER
    
             Contains the begin and end time for each trip (or NULL while
             trip is active. This table is there to provide fast access
             to questions about which trips where active when. The begin
             and end times are stored as unix timestamps (time_t values
             for example from the time() function). They are stored
             in GMT.

    There's a description of expected running times for each of the reporting
    queries below.

*/

/* Here are the queries for each of the reports: */

/* "- How many trips passed through a given geo-rect (defined by four 
       at/long pairs)."

   lat and long are the front of the index, and id is contained in that
   index. This should always be at worse O(log(n)).
*/
static char report1_sql[] = "SELECT COUNT(DISTINCT id) FROM triplog WHERE "
    "lat >= ? AND lat <= ? AND long >= ? AND long <= ?;";

/* "- How many trips started or stopped within a given geo-rect, and the
      sum of their fares."

    lat and long are at the front of the index, id and fare_cents are contained
    therin. This should always be at worst O(log(n)).
*/
static char report2_sql[] = "SELECT COUNT(DISTINCT id), SUM(fare_cents) FROM "
    "triplog WHERE lat >= ? AND lat <= ? AND long >= ? AND long <= ? AND "
    "(type = 0 OR type = 2);";


/* "- How many trips were occurring at a given point in time."

   begin and end are at the front of the index and id is contained therin. This
   should always be at worse O (log(n))
*/
static char report3_sql[] = "SELECT COUNT(DISTINCT id) FROM tripsummary WHERE "
                            "begin <= ? AND (end ISNULL OR end >= ?);";


/* DDL follows ... */

static char ddl_sql[] =
"CREATE TABLE triplog(id INTEGER,"
"                     long REAL,"
"                     lat REAL,"
"                     type INTEGER,"
"                     fare_cents INTEGER DEFAULT 0);"
"CREATE INDEX lat_long_idx ON triplog(lat, long, type, id, fare_cents);"
"CREATE INDEX type_idx ON triplog(id, type);"
"CREATE TABLE tripsummary(id INTEGER,"
"                         begin INTEGER,"
"                         end INTEGER);"
"CREATE INDEX summary_id_index ON tripsummary(id);"
"CREATE INDEX summary_time_index ON tripsummary(begin, end, id);";

static char insert_sql[] = "INSERT INTO triplog VALUES(?, ?, ?, ?, ?);";

static char insert_summary[] = "INSERT INTO tripsummary VALUES(?, ?, NULL);";

static char update_summary[] = "UPDATE tripsummary SET end = ? WHERE id = ?;";


/* open_create_db

   This opens our sqlite in memory database and runs the ddl to create
   our catalog.
*/

int
open_create_db(struct tripstore_context *ctx)
{
    ctx->db = NULL;
    int rc;
    char *errmsg = NULL;

    /* Create the in memory data store */
    rc = sqlite3_open(":memory:", &ctx->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open sqlite memory db: %s\n",
                sqlite3_errmsg(ctx->db));
        sqlite3_close(ctx->db);
        ctx->db = NULL;
        return -1;
    }

    /* Run our ddl to create the catalog */
    rc = sqlite3_exec(ctx->db, ddl_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to make ddl: %s\n", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(ctx->db);
        ctx->db = NULL;
        return -1;
    }
    return 0;
}

/* cleanup prepared statements */
void
finalize_one(sqlite3_stmt **s)
{
    if (*s) {
        sqlite3_finalize(*s);
        *s = NULL;
    }
}

/* shutdown the database and clean things up */
void
close_db(struct tripstore_context *ctx)
{
    int i;
    finalize_one(&ctx->insert);
    finalize_one(&ctx->insert_summary);
    finalize_one(&ctx->update_summary);
    for (i = 0; i < 3; i++) {
        finalize_one(&ctx->reports[i]);
    }

    sqlite3_close(ctx->db);
    ctx->db = NULL;
}

/* make sqlite prepared statement */
int
prepare_one(struct tripstore_context *ctx, const char *sql, sqlite3_stmt **stmt)
{
    int rc;
    rc = sqlite3_prepare_v2(ctx->db, sql, strlen(sql) + 1, stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "prepare: \"%s\": %s\n", sql, sqlite3_errstr(rc));
        return -1;
    }
}

/* prepare each of our reporting queries */
int
prepare_statements(struct tripstore_context *ctx)
{
    prepare_one(ctx, insert_sql, &ctx->insert);
    prepare_one(ctx, insert_summary, &ctx->insert_summary);
    prepare_one(ctx, update_summary, &ctx->update_summary);
    prepare_one(ctx, report1_sql, &ctx->reports[0]);
    prepare_one(ctx, report2_sql, &ctx->reports[1]);
    prepare_one(ctx, report3_sql, &ctx->reports[2]);
    return 0;
}

/* this is the entrypoint for all rows in the database */
int
add_tripdata(struct tripstore_context *ctx,
             int id, float lng, float lat, enum TRIP_EVENT_TYPE t, int cents)
{
    int rc;
    if (sqlite3_bind_int(ctx->insert, 1, id) != SQLITE_OK)
        goto fail;
    if (sqlite3_bind_double(ctx->insert, 2, lng) != SQLITE_OK)
        goto fail;
    if (sqlite3_bind_double(ctx->insert, 3, lat) != SQLITE_OK)
        goto fail;
    if (sqlite3_bind_int(ctx->insert, 4, t) != SQLITE_OK)
        goto fail;
    if (sqlite3_bind_int(ctx->insert, 5, cents) != SQLITE_OK)
        goto fail;

    /* When we add a BEGIN message, insert a new row into the tripsummary */
    if (t == BEGIN) {
        if (sqlite3_bind_int(ctx->insert_summary, 1, id) != SQLITE_OK)
            goto fail;
        if (sqlite3_bind_int(ctx->insert_summary, 2, time(NULL)) !=
                SQLITE_OK)
            goto fail;
        if (sqlite3_step(ctx->insert_summary) != SQLITE_DONE)
            goto fail;
        sqlite3_reset(ctx->insert_summary);
    /* When we add an END message, update the tripsummary row for the id */
    } else if (t == END) {
        if (sqlite3_bind_int(ctx->update_summary, 1, time(NULL)) !=
                SQLITE_OK)
            goto fail;
        if (sqlite3_bind_int(ctx->update_summary, 2, id) != SQLITE_OK)
            return -1;
        if (sqlite3_step(ctx->update_summary) != SQLITE_DONE)
            goto fail;
        sqlite3_reset(ctx->update_summary);
    }

    rc = sqlite3_step(ctx->insert);
    if (rc != SQLITE_DONE)
        goto fail;

    sqlite3_reset(ctx->insert);
    return 0;
fail:
    fprintf(stderr, "Failed to update tripdata!\n");
    return -1;
}

struct rowout_ctx
{
    struct tripstore_context *ctx;
    int fd;
};

/* Output this row data to the file descriptor provided. This is the handler
   for the sqlite3_exec() callback interface */
int
row_to_fd(void *pd, int cols, char **data, char **names)
{
    struct rowout_ctx *ctx = (struct rowout_ctx *)pd;
    int i;
    for (i = 0; i < cols; i++) {
        if (i != 0)
            write(ctx->fd, " ", 1);
        if (!data[i])
            write(ctx->fd, "NULL", strlen("NULL"));
        else
            write(ctx->fd, data[i], strlen(data[i]));
    }
    write(ctx->fd, "\n", 1);
    return 0;
}

/* Output this row data to the file descriptor provided. This is the handler
   for the sqlite3_step() iteration interface */
int
step_to_fd(sqlite3_stmt *stmt, int fd)
{
    int cols;
    const char *col;
    int collen;
    int i;
    while (SQLITE_ROW == sqlite3_step(stmt)) {
        cols = sqlite3_column_count(stmt);
        for (i = 0; i < cols; i++) {
            col = sqlite3_column_text(stmt, i);
            collen = sqlite3_column_bytes(stmt, i);
            if (i != 0)
                write(fd, " ", 1);
            if (!col)
                write(fd, "NULL", strlen("NULL"));
            else
                write(fd, col, collen);

        }
        write(fd, "\n", 1);
    }
    sqlite3_reset(stmt);
}

void
send_err_msg(int fd, const char *msg)
{
    write(fd, "error: ", strlen("error: "));
    write(fd, msg, strlen(msg));
    write(fd, "\n", 1);
}

/* Make sure that the one that should be lower is lower. If it isn't, swap
   them */
void
ensure_order(double *d1, double *d2)
{
    double tmp;
    if (*d1 > *d2) {
        tmp = *d1;
        *d1 = *d2;
        *d2 = tmp;
    }
}

void
bind4(sqlite3_stmt *stmt, double d1, double d2, double d3, double d4)
{
    ensure_order(&d1, &d2);
    ensure_order(&d3, &d4); 
    sqlite3_bind_double(stmt, 1, d1);
    sqlite3_bind_double(stmt, 2, d2);
    sqlite3_bind_double(stmt, 3, d3);
    sqlite3_bind_double(stmt, 4, d4);
}

/* Data is stored in the database in gmt. This converts a local time
   string to its gmt unixtime */

#define TIME_FORMAT "%Y-%m-%d %H:%M:%S"
time_t
localtime_to_gmt(const char *tstr)
{
    /* If they quoted the time, go ahead and skip the quote */
    if (*tstr == '\'' || *tstr == '"')
        tstr++;
    struct tm tm = {0, 0, 0, 0, 0, 0, 0, 0, -1, 0, NULL};
    strptime(tstr, TIME_FORMAT, &tm);
    return mktime(&tm);
}

/* This is the main handler for the query interface. We decide if they
   are running one of the reports, and if not then evaluate it as 
   freeform sql */
void
exec_query_tofd(const char *q, struct tripstore_context *ctx, int fd)
{
    float lat1, lat2, lng1, lng2;
    int replen = strlen("REPORTX");

    if (strncasecmp(q, "REPORT1", replen) == 0) {
        if (4 != sscanf(q + replen, " %f %f %f %f",
                        &lat1, &lat2, &lng1, &lng2)) {
            send_err_msg(fd, "REPORT1 takes lat1, lat2, long1, long2");
        } else {
            bind4(ctx->reports[0], lat1, lat2, lng1, lng2);
            step_to_fd(ctx->reports[0], fd);
        }
    } else if (strncasecmp(q, "REPORT2", replen) == 0) {
        if (4 != sscanf(q + replen, " %f %f %f %f",
                        &lat1, &lat2, &lng1, &lng2)) {
            send_err_msg(fd, "REPORT2 takes lat1, lat2, long1, long2");
        } else {
            bind4(ctx->reports[1], lat1, lat2, lng1, lng2);
            step_to_fd(ctx->reports[1], fd);
        }
    } else if (strncasecmp(q, "REPORT3", replen) == 0) {
        /* If they didn't give a date, then use now as the comparison */
        time_t t;
        if (strlen(q) <= replen + 1)
            t = time(NULL);
        else
            t = localtime_to_gmt(q + replen + 1);

        sqlite3_bind_int(ctx->reports[2], 1, t);
        sqlite3_bind_int(ctx->reports[2], 2, t);
        step_to_fd(ctx->reports[2], fd);
    } else {
        /* They aren't requesting a specific report so just treat the
           reset as plain SQL */
        char *errmsg;
        struct rowout_ctx *out = (struct rowout_ctx *)malloc(sizeof(*out));
        out->fd = fd;
        out->ctx = ctx;
        if (sqlite3_exec(ctx->db, q, row_to_fd, (void *)out,
                         &errmsg) != SQLITE_OK) {
            send_err_msg(out->fd, errmsg);
        }
        free(out);
    }
}

