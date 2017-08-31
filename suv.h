/*
 * suv.h - SiriDB C-Connector, example using libuv
 *
 *  Created on: Jun 09, 2017
 *      Author: Jeroen van der Heijden <jeroen@transceptor.technology>
 */

#ifndef SUV_H_
#define SUV_H_

#define SUV_VERSION_MAJOR 0
#define SUV_VERSION_MINOR 1
#define SUV_VERSION_PATCH 0

#define SUV_STRINGIFY(num) #num
#define SUV_VERSION_STR(major,minor,patch)   \
    SUV_STRINGIFY(major) "."                 \
    SUV_STRINGIFY(minor) "."                 \
    SUV_STRINGIFY(patch)

#define SUV_VERSION SUV_VERSION_STR(          \
        SUV_VERSION_MAJOR,                   \
        SUV_VERSION_MINOR,                   \
        SUV_VERSION_PATCH)

#include <stdlib.h>
#include <uv.h>
#include <libsiridb/siridb.h>

/* type definitions */
typedef struct suv_buf_s suv_buf_t;
typedef struct suv_write_s suv_write_t;
typedef struct suv_write_s suv_connect_t;
typedef struct suv_write_s suv_query_t;
typedef struct suv_write_s suv_insert_t;

/* public functions */
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*suv_cb) (void * buf_data, const char * msg);

suv_buf_t * suv_buf_create(siridb_t * siridb);
void suv_buf_destroy(suv_buf_t * suvbf);

void suv_write_destroy(suv_write_t * swrite);
void suv_write_error(suv_write_t * swrite, int err_code);

suv_connect_t * suv_connect_create(
    siridb_req_t * req,
    const char * username,
    const char * password,
    const char * dbname);
void suv_connect_destroy(suv_connect_t * connect);
void suv_connect(
    uv_loop_t * loop,
    suv_connect_t * connect,
    suv_buf_t * buf,
    struct sockaddr * addr);
void suv_close(suv_buf_t * buf, const char * msg);

suv_query_t * suv_query_create(siridb_req_t * req, const char * query);
void suv_query_destroy(suv_query_t * suvq);
void suv_query(suv_query_t * suvq);

suv_insert_t * suv_insert_create(
    siridb_req_t * req,
    siridb_series_t * series[],
    size_t n);
void suv_insert_destroy(suv_insert_t * insert);
void suv_insert(suv_insert_t * insert);

const char * suv_strerror(int err_code);
const char * suv_errproto(uint8_t tp);
const char * suv_version(void);

#define suv_buf_from_req(REQ__) \
    ((suv_buf_t *) ((uv_tcp_t *) REQ__->siridb->data)->data)

#ifdef __cplusplus
}
#endif

/* struct definitions */
struct suv_buf_s
{
    void * data;            /* public */
    suv_cb onclose;         /* public */
    suv_cb onerror;         /* public */
    char * buf;
    size_t len;
    size_t size;
    siridb_t * siridb;
};

struct suv_write_s
{
    void * data;            /* public */
    siridb_pkg_t * pkg;     /* packge to send */
    siridb_req_t * _req;    /* will not be cleared */
};

#endif /* SUV_H_ */
