# SiriDB Connector using libuv and libsiridb.
This example is written as an extension to libsiridb which we will call libsuv.

---------------------------------------
  * [API](#api)
    * [suv_buf_t](#suv_buf_t)
    * [suv_write_t](#suv_write_t)
    * [suv_connect_t](#suv_connect_t)
    * [suv_query_t](#suv_query_t)
    * [suv_insert_t](#suv_insert_t)
    * [Miscellaneous functions](#miscellaneous-functions)

---------------------------------------

## API
>Note: libsuv uses some of the public data members which are exposed by libsiridb.
>In most cases libsuv exposes its own public space for user-defined data which
>should be used instead.

### `suv_buf_t`
Buffer type. Each SiriDB connection must have its own buffer.

#### `suv_buf_t * suv_buf_create(siridb_t * siridb)`
Create and return a new buffer. A `siridb_t` instance is required.

>Warning: Do not use `siridb->data` since it will be overwritten as soon as the
>buffer is used as a connection.

*Public members*
- `void * suv_buf_t.data`: Space for user-defined arbitrary data. libsuv does
not use this field.
- `suv_cb onclose`: Can be set to an optional callback function which will be
called when a connection is closed.
- `suv_cb onerror`: Can be set to an optional callback function which will be
called when the normal request callback (`siridb_on_pkg()`) returns with an error.

#### `suv_buf_from_req(siridb_req_t * req)`
Macro function to get the `suv_buf_t*` from a request.

#### `void suv_buf_destroy(suv_buf_t * suvbf)`
Cleanup a buffer. Call this function after the connection is closed.

#### `void suv_close(suv_buf_t * buf, const char * msg)`
Close a connection. As long as the buffer is not destroyed, the same `suv_buf_t`
can be used again to create a new connection. Argument `msg` is allowed to be
`NULL` in wich case the default message will be used. The message (or default message)
will be parsed to the `suv_buf_t.onclose` callback function.

### `suv_write_t`
General write type. Used to communicate with SiriDB.

*Public members*
- `void * suv_write_t.data`: Space for user-defined arbitrary data. libsuv does
not use this field.
- `void * suv_write_t.pkg`: Contains the package to send. (readonly)

#### `void suv_write_destroy(suv_connect_t * connect)`
Cleanup a write handle. This function should be called from a request
(`siridb_req_t`) callback function.

#### `void suv_write_error(suv_write_t * swrite, int err_code)`
Used to cancel a write handle. This will set the request status to `err_code`,
removes the request from the queue and calls the request callback function.

### `suv_connect_t`
Connect handle. Alias for `suv_write_t`.

#### `suv_connect_t * suv_connect_create(siridb_req_t * req, const char * username, const char * password, const char * dbname)`
Create and return a connection handle. After the connection handle is created,
you must manually bind the handle to `req->data`. This must be done explicit to
make clear that you are also responsible for handling the cleanup.

Returns `NULL` in case of a memory allocation error.

#### `void suv_connect_destroy(suv_connect_t * connect)`
Cleaunp a connection handle. This function should be called from a request
(`siridb_req_t`) callback function. Alias for `suv_write_destroy()`.

#### `void suv_connect(uv_loop_t * loop, suv_connect_t * connect, suv_buf_t * buf, struct sockaddr * addr)`
Connect and authenticate to SiriDB.

>Warning: This function overwrites the members `buf->siridb->data` so
>you should not use this property. Instead the public members `buf->data` and
>`connect->data` are available and safe to use.

Example:
```c
#include <suv.h>

uv_loop_t loop;

void connect_cb(siridb_req_t * req)
{
    suv_connect_t * connect = (suv_connect_t *) req->data;

    if (req->status) {
        printf("connect or auth failed: %s\n", siridb_strerror(req->status));
    } else if (req->pkg->tp != CprotoResAuthSuccess) {
        printf("authentication failed (error %u)\n", req->pkg->tp);
    } else {
        // do something with the connection
    }

    /* cleanup connetion handle */
    suv_connect_destroy(connect);

    /* lets stop the example */
    suv_close(suv_buf_from_req(req), NULL);

    /* cleanup connection request */
    siridb_req_destroy(req);
}

int main(void)
{
    struct sockaddr_in addr;
    /* initialize uv loop */
    uv_loop_init(&loop);

    /* asume siridb-server is running on localhost and port 9000 */
    uv_ip4_addr("127.0.0.1", 9000, &addr);

    /* create a siridb client */
    siridb_t * siridb = siridb_create();

    /* create a buffer for the connection */
    suv_buf_t * buf = suv_buf_create(siridb);

    /* create a connection request */
    siridb_req_t * req = siridb_req_create(siridb, connect_cb, NULL);

    /* create a connection handle */
    suv_connect_t * connect = suv_connect_create(req, "iris", "siri", "dbtest");

    /* explicit bind the connect handle to the request. (this must be done!) */
    req->data = (void *) connect;

    /* Warning: This overwrites tcp->data and siridb->data so do not use these
     *          members yourself. */
    suv_connect(&loop, connect, buf, (struct sockaddr *) &addr);

    /* run the uv event loop */
    uv_run(&loop, UV_RUN_DEFAULT);

    /* close the loop */
    uv_loop_close(&loop);

    /* cleanup buffer */
    suv_buf_destroy(buf);

    /* cleanup siridb */
    siridb_destroy(siridb);

    return 0;
}
```

### `suv_query_t`
Query handle. Alias for `suv_write_t`.

#### `suv_query_t * suv_query_create(siridb_req_t * req, const char * query)`
Create and return a query handle. After the query handle is created,
you must manually bind the handle to `req->data`. This must be done explicit to
make clear that you are also responsible for handling the cleanup.

Returns `NULL` in case of a memory allocation error.

#### `void suv_query_destroy(suv_query_t * suvq)`
Cleaunp a query handle. This function should be called from a request
(`siridb_req_t`) callback function. Alias for `suv_write_destroy()`.

#### `void suv_query(suv_query_t * suvq)`
Query SiriDB.

Example
```c
/* first create a request */
siridb_req_t * req = siridb_req_create(siridb, example_cb, NULL);

/* create query handle */
suv_query_t * handle = suv_query_create(req, "select * from 'my-series'");

/* bind query handle to req->data */
req->data = (void *) handle;

/* now run the query, check the callback for the result or errors. */
suv_query(handle);
```

Example callback function:
```c
void example_cb(siridb_req_t * req)
{
    if (req->status != 0) {
        printf("error handling request: %s", siridb_strerror(req->status));
    } else {
        /* get the response */
        siridb_resp_t * resp = siridb_resp_create(req->pkg, NULL);

        // do something with the response...

        // a general cb function could do something based on the response type...
        switch(resp->tp) {
        case SIRIDB_RESP_TP_UNDEF:
        case SIRIDB_RESP_TP_SELECT:
        case SIRIDB_RESP_TP_LIST:
        case SIRIDB_RESP_TP_SHOW:
        case SIRIDB_RESP_TP_COUNT:
        case SIRIDB_RESP_TP_CALC:
        case SIRIDB_RESP_TP_SUCCESS:
        case SIRIDB_RESP_TP_SUCCESS_MSG:
        case SIRIDB_RESP_TP_ERROR:
        case SIRIDB_RESP_TP_ERROR_MSG:
        case SIRIDB_RESP_TP_HELP:
        case SIRIDB_RESP_TP_MOTD:
        case SIRIDB_RESP_TP_DATA: break;
        }

        /* cleanup response */
        siridb_resp_destroy(resp);
    }

    /* destroy handle */
    suv_write_destroy((suv_write_t *) req->data);

    /* destroy request */
    siridb_req_destroy(req);
}
```

### `suv_insert_t`
Insert handle. Alias for `suv_write_t`.

#### `suv_insert_t * suv_insert_create(siridb_req_t * req, siridb_series_t * series[], size_t n)`
Create and return an insert handle. Argument `n` must be equal or smaller than
the number of series in the `series[]` array.

After the insert handle is created, you must manually bind the handle to
`req->data`. This must be done explicit to make clear that you are also
responsible for handling the cleanup.

Returns `NULL` in case of a memory allocation error.

#### `void suv_insert_destroy(suv_insert_t * insert)`
Cleanup an insert handle. This function should be called from a request
(`siridb_req_t`) callback function. Alias for `suv_write_destroy()`.

#### `void suv_insert(suv_insert_t * insert`
Insert data into SiriDB.

Example:
```c
/* create an array of series, just one for this example */
siridb_series_t * series[1];

/* create series */
series[0] = siridb_series_create(
    SIRIDB_SERIES_TP_INT64,         /* type int64, could also be double */
    "example-series-name",          /* some name for the series */
    10);                            /* number of points */

/* create some sample points */
for (size_t i = 0; i < series[0]->n; i++) {
    siridb_point_t * point = series[0]->points + i;
    point->ts = (uint64_t) time(NULL) - series[0]->n + i;
    point->via.int64 = (int64_t) i;
}

/* create a request */
siridb_req_t * req = siridb_req_create(siridb, example_cb, NULL);

/* create insert handle */
suv_insert_t * handle = suv_insert_create(req, series, 1);

/* bind the handle to the request */
req->data = (void *) handle;

/* cleanup the series since the series is now packed in handle->pkg */
siridb_series_destroy(series[0]);

/* insert data into siridb, the callback should be checked for errors.
 * (a successful insert has a SIRIDB_RESP_TP_SUCCESS_MSG siridb_resp_t.tp) */
suv_insert(handle);
```

### Miscellaneous functions
#### `const char * suv_strerror(int err_code)`
Returns the error message for a given error code.

#### `const char * suv_version(void)`
Returns the version of libsuv.

#### `const char * suv_errproto(uint8_t tp)`
Returns a JSON compatible string for a package response type. Since not all
return packages from SiriDB contain JSON data, this function can be used to
add an appropriate JSON string to those responses which lack this data.