/*
 * suv.c - SiriDB C-Connector libuv
 *
 *  Created on: Aug 31, 2017
 *      Author: Jeroen van der Heijden <jeroen@transceptor.technology>
 */

#include "suv.h"
#include <string.h>
#include <assert.h>

static suv_write_t * suv__write_create(void);
static void suv__close_tcp(uv_handle_t * tcp);
static void suv__write(suv_write_t * swrite);
static void suv__alloc_buf(uv_handle_t * handle, size_t sugsz, uv_buf_t * buf);
static void suv__on_data(uv_stream_t * clnt, ssize_t n, const uv_buf_t * buf);
static void suv__write_cb(uv_write_t * uvreq, int status);
static void suv__connect_cb(uv_connect_t * uvreq, int status);

const long int MAX_PKG_SIZE = 209715200; // can be changed to anything you want

/*
 * Return libsuv version info.
 */
const char * suv_version(void)
{
    return SUV_VERSION;
}

/*
 * Create and returns a buffer object or NULL in case of an allocation error.
 */
suv_buf_t * suv_buf_create(siridb_t * siridb)
{
    suv_buf_t * suvbf = (suv_buf_t *) malloc(sizeof(suv_buf_t));
    if (suvbf != NULL)
    {
        suvbf->siridb = siridb;
        suvbf->len = 0;
        suvbf->size = 0;
        suvbf->buf = NULL;
        suvbf->onclose = NULL;
        suvbf->onerror = NULL;
    }
    return suvbf;
}

/*
 * Destroy a buffer object.
 */
void suv_buf_destroy(suv_buf_t * suvbf)
{
    uv_tcp_t * tcp_ = (uv_tcp_t *) suvbf->siridb->data;
    if (tcp_ != NULL)
    {
        suv_close(suvbf, NULL);
    }
    free(suvbf->buf);
    free(suvbf);
}

/*
 * Create and return an connect object or NULL in case of an allocation error.
 */
suv_connect_t * suv_connect_create(
    siridb_req_t * req,
    const char * username,
    const char * password,
    const char * dbname)
{
    assert (req->data == NULL); /* req->data should be set to -this- */

    suv_write_t * connect = suv__write_create();
    if (connect != NULL)
    {
        connect->pkg = siridb_pkg_auth(req->pid, username, password, dbname);
        connect->_req = req;
        if (connect->pkg == NULL)
        {
            suv_write_destroy(connect);
            connect = NULL;
        }
    }
    return (suv_connect_t *) connect;
}

/*
 * Destroy an connect object.
 */
void suv_connect_destroy(suv_connect_t * connect)
{
    suv_write_destroy((suv_write_t * ) connect);
}

/*
 * Use this function to connect to SiriDB. Always use the callback defined by
 * the request object parsed to suv_connect_create() for errors.
 */
void suv_connect(
    uv_loop_t * loop,
    suv_connect_t * connect,
    suv_buf_t * buf,
    struct sockaddr * addr)
{
    assert (connect->_req->data == connect);  /* bind connect to req->data */
    assert (buf->siridb->data == NULL);  /* siridb data should be null, maybe
                                            the connection is still in use? */

    uv_connect_t * uvreq = (uv_connect_t *) malloc(sizeof(uv_connect_t));
    if (uvreq == NULL)
    {
        suv_write_error((suv_write_t *) connect, ERR_MEM_ALLOC);
        return;
    }

    uv_tcp_t * tcp_ = (uv_tcp_t *) malloc(sizeof(uv_tcp_t));
    if (tcp_ == NULL)
    {
        suv_write_error((suv_write_t *) connect, ERR_MEM_ALLOC);
        return;
    }

    tcp_->data = (void *) buf;
    buf->siridb->data = (void *) tcp_;

    uv_tcp_init(loop, tcp_);

    uvreq->data = (void *) connect->_req;
    uv_tcp_connect(uvreq, tcp_, addr, suv__connect_cb);
}

/*
 * Close an open connection.
 */
void suv_close(suv_buf_t * buf, const char * msg)
{
    uv_tcp_t * tcp_ = (uv_tcp_t *) buf->siridb->data;
    if (!uv_is_closing((uv_handle_t *) tcp_))
    {
        if (buf->onclose != NULL)
        {
            buf->onclose(buf->data, (msg == NULL) ? "connection closed" : msg);
        }
        uv_close((uv_handle_t *) tcp_, suv__close_tcp);
    }
}

/*
 * Create and return a query object or NULL in case of an allocation error.
 */
suv_query_t * suv_query_create(siridb_req_t * req, const char * query)
{
    assert (req->data == NULL); /* req->data should be set to -this- */

    suv_write_t * suvq = suv__write_create();
    if (suvq != NULL)
    {
        suvq->pkg = siridb_pkg_query(req->pid, query);
        suvq->_req = req;
        if (suvq->pkg == NULL)
        {
            suv_write_destroy(suvq);
            suvq = NULL;
        }
    }
    return (suv_query_t *) suvq;
}

/*
 * Destroy a query object.
 */
void suv_query_destroy(suv_query_t * suvq)
{
    suv_write_destroy((suv_write_t * ) suvq);
}

/*
 * This function actually runs a query. Always use the callback defined by
 * the request object parsed to suv_query_create() for errors.
 */
void suv_query(suv_query_t * suvq)
{
    suv__write((suv_write_t *) suvq);
}

/*
 * Create and return a insert object or NULL in case of an allocation error.
 */
suv_insert_t * suv_insert_create(
    siridb_req_t * req,
    siridb_series_t * series[],
    size_t n)
{
    assert (req->data == NULL); /* req->data should be set to -this- */

    suv_write_t * insert = suv__write_create();
    if (insert != NULL)
    {
        insert->pkg = siridb_pkg_series(req->pid, series, n);
        insert->_req = req;
        if (insert->pkg == NULL)
        {
            suv_write_destroy(insert);
            insert = NULL;
        }
    }
    return (suv_insert_t *) insert;
}

/*
 * Destroy a insert object.
 */
void suv_insert_destroy(suv_insert_t * insert)
{
    suv_write_destroy((suv_write_t * ) insert);
}

/*
 * This function actually send the insert. Always use the callback defined by
 * the request object parsed to suv_insert_create() for errors.
 */
void suv_insert(suv_insert_t * insert)
{
    suv__write((suv_write_t *) insert);
}

/*
 * Create and return a write object or NULL in case of an allocation error.
 */
static suv_write_t * suv__write_create(void)
{
    suv_write_t * swrite = (suv_write_t *) malloc(sizeof(suv_write_t));
    if (swrite != NULL)
    {
        swrite->data = NULL;
        swrite->pkg = NULL;
    }
    return swrite;
}

/*
 * Close and free handle.
 */
static void suv__close_tcp(uv_handle_t * tcp)
{
    if (tcp->data != NULL)
    {
        suv_buf_t * buf = (suv_buf_t *) tcp->data;
        buf->siridb->data = NULL;
    }
    free(tcp);
}

/*
 * Destroy a write object.
 */
void suv_write_destroy(suv_write_t * swrite)
{
    free(swrite->pkg);
    free(swrite);
}

/*
 * Set request error and run callback
 */
void suv_write_error(suv_write_t * swrite, int err_code)
{
    queue_pop(swrite->_req->siridb->queue, swrite->pkg->pid);
    swrite->_req->status = err_code;
    swrite->_req->cb(swrite->_req);
}

/*
 * Return error string. UV errors should be set to positive values.
 */
const char * suv_strerror(int err_code)
{
    return (err_code > 0) ?
            uv_strerror(-err_code) : siridb_strerror(err_code);
}

/*
 * This function actually send the data.
 */
static void suv__write(suv_write_t * swrite)
{
    assert (swrite->_req->data == swrite); /* bind swrite to req->data */

    uv_stream_t * stream = (uv_stream_t *) swrite->_req->siridb->data;
    if (stream == NULL)
    {
        suv_write_error(swrite, ERR_SOCK_WRITE);
        return;
    }

    uv_write_t * uvreq = (uv_write_t *) malloc(sizeof(uv_write_t));
    if (uvreq == NULL)
    {
        suv_write_error(swrite, ERR_MEM_ALLOC);
        return;
    }

    uvreq->data = (void *) swrite->_req;

    uv_buf_t buf = uv_buf_init(
        (char *) swrite->pkg,
        sizeof(siridb_pkg_t) + swrite->pkg->len);

    uv_write(uvreq, stream, &buf, 1, suv__write_cb);
}

static void suv__connect_cb(uv_connect_t * uvreq, int status)
{
    siridb_req_t * req = (siridb_req_t *) uvreq->data;
    suv_connect_t * connect = (suv_connect_t *) req->data;

    if (status != 0)
    {
        /* error handling */
        suv_write_error((suv_write_t *) connect, -status);
    }
    else
    {
        uv_write_t * uvw = (uv_write_t *) malloc(sizeof(uv_write_t));
        if (uvw == NULL)
        {
            suv_write_error((suv_write_t *) connect, ERR_MEM_ALLOC);
        }
        else
        {
            uvw->data = (void *) req;

            uv_buf_t buf = uv_buf_init(
                    (char *) connect->pkg,
                    sizeof(siridb_pkg_t) + connect->pkg->len);

            uv_read_start(uvreq->handle, suv__alloc_buf, suv__on_data);
            uv_write(uvw, uvreq->handle, &buf, 1, suv__write_cb);
        }
    }
    free(uvreq);
}

static void suv__alloc_buf(uv_handle_t * handle, size_t sugsz, uv_buf_t * buf)
{
    suv_buf_t * suvbf = (suv_buf_t *) handle->data;
    if (suvbf->len == 0 && suvbf->size != sugsz)
    {
        free(suvbf->buf);
        suvbf->buf = (char *) malloc(sugsz);
        if (suvbf->buf == NULL)
        {
            abort(); /* memory allocation error */
        }
        suvbf->size = sugsz;
        suvbf->len = 0;
    }

    buf->base = suvbf->buf + suvbf->len;
    buf->len = suvbf->size - suvbf->len;
}

static void suv__on_data(uv_stream_t * clnt, ssize_t n, const uv_buf_t * buf)
{
    suv_buf_t * suvbf = (suv_buf_t *) clnt->data;
    siridb_pkg_t * pkg;
    size_t total_sz;
    int rc;

    if (n < 0)
    {
        suv_close(suvbf, (n != UV_EOF) ? uv_strerror(n) : NULL);
        return;
    }

    suvbf->len += n;

    if (suvbf->len < sizeof(siridb_pkg_t))
    {
        return;
    }

    pkg = (siridb_pkg_t *) suvbf->buf;
    if (!siridb_pkg_check_bit(pkg) || pkg->len > MAX_PKG_SIZE)
    {
        suv_close(suvbf, "invalid package, connection closed");
        return;
    }

    total_sz = sizeof(siridb_pkg_t) + pkg->len;

    if (suvbf->len < total_sz)
    {
        if (suvbf->size < total_sz)
        {
            char * tmp = realloc(suvbf->buf, total_sz);
            if (tmp == NULL)
            {
                abort(); /* memory allocation error */
            }
            suvbf->buf = tmp;
            suvbf->size = total_sz;
        }
        return;
    }

    if ((rc = siridb_on_pkg(suvbf->siridb, pkg)))
    {
        if (suvbf->onerror != NULL)
        {
            suvbf->onerror(suvbf->data, siridb_strerror(rc));
        }
    }

    suvbf->len -= total_sz;

    if (suvbf->len > 0)
    {
        /* move data and call suv_on_data() function again */
        memmove(suvbf->buf, suvbf->buf + total_sz, suvbf->len);
        suv__on_data(clnt, 0, buf);
    }
}

static void suv__write_cb(uv_write_t * uvreq, int status)
{
    if (status)
    {
        /* error handling */
        siridb_req_t * req = (siridb_req_t *) uvreq->data;
        suv_write_t * swrite = (suv_write_t *) req->data;

        suv_write_error(swrite, -status);
    }

    /* free uv_write_t */
    free(uvreq);
}


