#include <stdio.h>
#include <string.h>
#include "msgs.h"

#define MSG_HDR_SIZE (sizeof(int) * 2)

/*
   The message format:

   message size (int), messag type (int), <optional fields>
*/

/* Helper functions */

/* Fill in the header portion for the generate messages */
char *
msg_hdr(char *p, int size, int t)
{
    size += MSG_HDR_SIZE;
    memcpy(p, &size, sizeof(size));
    p += sizeof(size);
    memcpy(p, &t, sizeof(t));
    p += sizeof(t);
    return p;
}

/* Send all data - handles short sends */
int
full_send(int s, char *buf, int size)
{
    int x = 0;
    int got;
    got = write(s, buf, size);
    while (got > 0) {
        x += got;
        if (x == size)
            return 0;
        got = write(s, buf + x, size - x);
    }
    return -1;
}

/* messages commonly have lat and long - use this helper */
char *
add_lng_lat(char *p, float lng, float lat)
{
    memcpy(p, &lng, sizeof(lng));
    p += sizeof(float);
    memcpy(p, &lat, sizeof(lat));
    p += sizeof(float);

    return p;
}

/* messages commonly have id,  lat, long - use this helper */
char *
add_id_lng_lat(char *p, int id, float lng, float lat)
{
    memcpy(p, &id, sizeof(id));
    p += sizeof(id);
    return add_lng_lat(p, lng, lat);
}

/* add the cents field. This could more generally be just add_int() */
char *
add_cents(char *p, int cents)
{
    memcpy(p, &cents, sizeof(cents));
    p += sizeof(cents);
    return p;
}


/* Entry points for sending each of the message types */

/* begin message means new generator starting up */
int
send_begin_msg(int s, float lng, float lat)
{
    char buf[MSG_HDR_SIZE + 2 * sizeof(float)];
    char *p = buf;

    p = msg_hdr(p, sizeof(float) * 2, MSG_BEGIN);
    p = add_lng_lat(p, lng, lat);

    if (-1 == full_send(s, buf, p - buf)) {
        return -1;
    }
    return 0;
}

/* after sending the begin, generator waits for the trip id to be assigned */
int
recv_trip_id(int s)
{
    char buf[MSG_HDR_SIZE + sizeof(int)];
    int got;
    int x = 0;
    int size = MSG_HDR_SIZE + sizeof(int);

    got = read(s, buf, size);
    while (got > 0) {
        x += got;
        if (x == size)
            break;
        got = read(s, buf + x, size - x);
    }

    if (x == size) {
        char * p = buf + MSG_HDR_SIZE;
        return *(int *)p;
    }
    return 0;
}

/* the server replies with the allocated trip id */
int
send_trip_id(int s, int id)
{
    char buf[MSG_HDR_SIZE + sizeof(id)];
    char * p = buf;
    
    p = msg_hdr(p, sizeof(int), MSG_ID);
    memcpy(p, &id, sizeof(id));
    p += sizeof(id);

    if (-1 == full_send(s, buf, p - buf))
        return -1;
    return 0;
}

/* generator sends these for each 1 second update */
int
send_update_msg(int s, int id, float lng, float lat)
{
    char buf[MSG_HDR_SIZE + sizeof(id) + sizeof(float) * 2];
    char *p = buf;

    p = msg_hdr(p, sizeof(id) + sizeof(float) * 2, MSG_UPDATE);
    p = add_id_lng_lat(p, id, lng, lat);

    if (-1 == full_send(s, buf, p - buf))
        return -1;
    return 0;
}

/* generator sends this when the trip is complete */
int
send_end_msg(int s, int id, float lng, float lat, int cents)
{
    char buf[MSG_HDR_SIZE * sizeof(int) * 2 + sizeof(float) * 2];
    char *p = buf;

    p = msg_hdr(p, sizeof(int) * 2 + sizeof(float) * 2, MSG_END);
    p = add_id_lng_lat(p, id, lng, lat);
    p = add_cents(p, cents);

    if (-1 == full_send(s, buf, p - buf))
        return -1;
    return 0;
}

/* server (stripsore) utility function to parse out the message fields */
int
parse_msg(char *buf, int size,
          enum MSG_TYPE *t, int *id, float *lng, float *lat, int *cents)
{
    char *oldp;
    char *p = buf;
    /* Skip the size marker */
    p += sizeof(int);

    int type;
    memcpy(&type, p, sizeof(type));
    p += sizeof(type);
    *t = type;

    switch (type) {
        case MSG_END:
            oldp = p;
            p += sizeof(int) + 2 * sizeof(float);
            memcpy(cents, p, sizeof(int));
            p = oldp;
            /* fall through */
        case MSG_UPDATE:
            memcpy(id, p, sizeof(int));
            p += sizeof(int);
            /* fall through */
        case MSG_BEGIN:
            memcpy(lng, p, sizeof(float));
            p += sizeof(float);
            memcpy(lat, p, sizeof(float));
            p += sizeof(float);
            break;

        default:
            return -1;
    }

    return 0;
}

