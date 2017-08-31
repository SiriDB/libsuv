/*
 * main.c
 *    SiriDB C-Connector example using libsuv. This is a quite large example
 *    since we try to cover most features from libsiridb and libsuv.
 *
 *  WARNING:
 *     Be carefull runing this example since it will query and insert data into
 *     SiriDB!
 *
 *  Compile using:
 *
 *     gcc main.c -lsuv -lsiridb -lqpack -luv -o example.out
 *
 *  Created on: Aug 31, 2017
 *      Author: Jeroen van der Heijden <jeroen@transceptor.technology>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <suv.h>

static uv_loop_t loop;

/* Change this values to your needs */
const char * SERVER     = "127.0.0.1";
const int    PORT       = 9000;
const char * USER       = "iris";
const char * PASSWD     = "siri";
const char * DBNAME     = "dbtest";
const char * QUERY      = "show";

static void connect_cb(siridb_req_t * req);
static void query_cb(siridb_req_t * req);
static void insert_cb(siridb_req_t * req);
static void insert_example(siridb_t * siridb);
static void send_example_query(siridb_t * siridb, const char * query);
static void print_resp(siridb_resp_t * resp);
static void print_timeit(siridb_timeit_t * timeit);
static void print_select(siridb_select_t * select);
static void print_list(siridb_list_t * list);
static void print_count(uint64_t count);
static void print_calc(uint64_t calc);
static void print_show(siridb_show_t * show);
static void print_msg(const char * msg);
static void on_close(void * data, const char * msg);
static void on_error(void * data, const char * msg);

int main(void)
{
    printf("Running example using:\n"
           " - libqpack %s\n"
           " - libsiridb %s\n"
           " - libsuv %s\n"
           "\n", qp_version(), siridb_version(), suv_version());

    struct sockaddr_in addr;
    char * query = strdup(QUERY);

    uv_loop_init(&loop);

    uv_ip4_addr(SERVER, PORT, &addr);

    siridb_t * siridb = siridb_create();
    /* handle siridb == NULL */
    /* Warning: do not use siridb->data since it will be used by libsuv */

    suv_buf_t * buf = suv_buf_create(siridb);
    /* handle buf == NULL */

    /* set optional callback functions */
    buf->onclose = on_close;
    buf->onerror = on_error;

    siridb_req_t * req = siridb_req_create(siridb, connect_cb, NULL);
    /* handle req == NULL */

    suv_connect_t * connect = suv_connect_create(req, USER, PASSWD, DBNAME);
    /* handle connect == NULL */

    /* for the example we bind a query string but this could be anything */
    connect->data = (void *) query;
    req->data = (void *) connect;

    suv_connect(&loop, connect, buf, (struct sockaddr *) &addr);

    uv_run(&loop, UV_RUN_DEFAULT);

    /* cleanup buffer */
    suv_buf_destroy(buf);

    /* cleanup siridb */
    siridb_destroy(siridb);

    /* close uv loop */
    uv_loop_close(&loop);

    return 0;
}

static void connect_cb(siridb_req_t * req)
{
    suv_connect_t * connect = (suv_connect_t *) req->data;
    /* handle connect == NULL (for example in case the request is cancelled
     * before a connection handle was attached) */

    char * query = (char *) connect->data;
    if (req->status)
    {
        printf("connect failed: %s\n", suv_strerror(req->status));
    }
    else
    {
        switch (req->pkg->tp)
        {
        case CprotoResAuthSuccess:
            send_example_query(req->siridb, query);
            break;
        case CprotoErrAuthCredentials:
            printf("auth failed: invalid credentials\n");
            break;
        case CprotoErrAuthUnknownDb:
            printf("auth failed: unknown database\n");
            break;
        default:
            printf("auth failed: unknown error (%u)\n", req->pkg->tp);
        }
    }

    /* free query string */
    free(query);

    /* destroy suv_connect_t */
    suv_connect_destroy(connect);

    /* destroy siridb request */
    siridb_req_destroy(req);
}

static void query_cb(siridb_req_t * req)
{
    siridb_t * siridb = req->siridb;

    if (req->status != 0)
    {
        printf("error handling request: %s", suv_strerror(req->status));
    }
    else
    {
        /* We can get the output as a JSON string... */
        char * json = qp_sprint(req->pkg->data, req->pkg->len);
        printf("Response as JSON:\n%s\n\n", json);
        free(json);

        /* ..or get a nice response object */
        siridb_resp_t * resp = siridb_resp_create(req->pkg, NULL);
        /* handle resp == NULL or use rc code for details */
        print_resp(resp);

        siridb_resp_destroy(resp);
    }

    /* destroy suv_query_t */
    suv_query_destroy((suv_query_t *) req->data);

    /* destroy siridb request */
    siridb_req_destroy(req);

    /* call the insert example */
    insert_example(siridb);
}

static void insert_cb(siridb_req_t * req)
{
    if (req->status != 0)
    {
        printf("error handling request: %s", suv_strerror(req->status));
    }
    else
    {
        siridb_resp_t * resp = siridb_resp_create(req->pkg, NULL);
        /* handle resp == NULL or use rc code for details */
        print_resp(resp);

        siridb_resp_destroy(resp);
    }

    /* destroy suv_insert_t */
    suv_insert_destroy((suv_insert_t *) req->data);

    /* here we quit the example by closing the handle */
    suv_close(suv_buf_from_req(req), NULL);

    /* destroy siridb request */
    siridb_req_destroy(req);
}

static void insert_example(siridb_t * siridb)
{
    siridb_series_t * series[2]; /* in this example we insert 2 series */
    siridb_point_t * point;

    series[0] = siridb_series_create(
        SIRIDB_SERIES_TP_INT64,         /* type integer */
        "c-conn-int64-test-series",     /* some name for the series */
        10);                            /* number of points */

    for (size_t i = 0; i < series[0]->n; i++)
    {
        point = series[0]->points + i;
        /* set the time-stamp */
        point->ts = (uint64_t) time(NULL) - series[0]->n + i;
        /* set a value, just the values 0 to 9 in this example */
        point->via.int64 = (int64_t) i;
    }

    series[1] = siridb_series_create(
        SIRIDB_SERIES_TP_REAL,          /* type float */
        "c-conn-real-test-series",      /* some name for the series */
        5);                             /* number of points */

    for (size_t i = 0; i < series[1]->n; i++)
    {
        point = series[1]->points + i;
        /* set the time-stamp */
        point->ts = (uint64_t) time(NULL) - series[1]->n + i;
        /* set a value, just the values 0.0 to 0.4 in this example */
        point->via.real = (double) i / 10;
    }

    siridb_req_t * req = siridb_req_create(siridb, insert_cb, NULL);
    /* handle req == NULL */

    suv_insert_t * suvinsert = suv_insert_create(req, series, 2);
    /* handle suvinsert == NULL */

    /* cleanup the series, we don't need them anymore */
    siridb_series_destroy(series[0]);
    siridb_series_destroy(series[1]);

    /* bind suvinsert to qreq->data */
    req->data = (void *) suvinsert;

    suv_insert(suvinsert);
    /* check insert_cb for errors */
}

static void send_example_query(siridb_t * siridb, const char * query)
{
    siridb_req_t * req = siridb_req_create(siridb, query_cb, NULL);
    /* handle req == NULL */

    suv_query_t * suvquery = suv_query_create(req, query);
    /* handle suvquery == NULL */

    /* bind suvquery to req->data */
    req->data = (void *) suvquery;

    suv_query(suvquery);
    /* check query_cb for errors */
}

static void on_close(void * data, const char * msg)
{
    printf("%s\n", msg);
}

static void on_error(void * data, const char * msg)
{
    printf("got an error: %s\n", msg);
}

static void print_resp(siridb_resp_t * resp)
{
    print_timeit(resp->timeit);

    switch (resp->tp)
    {
    case SIRIDB_RESP_TP_SELECT:
        print_select(resp->via.select); break;
    case SIRIDB_RESP_TP_LIST:
        print_list(resp->via.list); break;
    case SIRIDB_RESP_TP_COUNT:
        print_count(resp->via.count); break;
    case SIRIDB_RESP_TP_CALC:
        print_calc(resp->via.calc); break;
    case SIRIDB_RESP_TP_SHOW:
        print_show(resp->via.show); break;
    case SIRIDB_RESP_TP_SUCCESS:
        print_msg(resp->via.success); break;
    case SIRIDB_RESP_TP_SUCCESS_MSG:
        print_msg(resp->via.success_msg); break;
    case SIRIDB_RESP_TP_ERROR:
        print_msg(resp->via.error); break;
    case SIRIDB_RESP_TP_ERROR_MSG:
        print_msg(resp->via.error_msg); break;
    default: assert(0);
    }
}

static void print_timeit(siridb_timeit_t * timeit)
{
    if (timeit != NULL)
    {
        printf("Query time: %f seconds\n", timeit->perfs[timeit->n - 1].time);
        for (size_t i = 0; i < timeit->n; i++)
        {
            printf(
                "    server: %s time: %f\n",
                timeit->perfs[i].server,
                timeit->perfs[i].time);
        }
        printf("\n");
    }
}

static void print_select(siridb_select_t * select)
{
    printf("Select response for %" PRIu64 " series:\n", select->n);
    for (size_t m = 0; m < select->n; m++)
    {
        siridb_series_t * series = select->series[m];

        printf("    series: '%s'\n", series->name);
        for (size_t i = 0; i < series->n; i++)
        {
            printf(
                "        timestamp: %" PRIu64 " value: ",
                series->points[i].ts);
            switch (series->tp)
            {
            case SIRIDB_SERIES_TP_INT64:
                printf("%ld\n", series->points[i].via.int64); break;
            case SIRIDB_SERIES_TP_REAL:
                printf("%f\n", series->points[i].via.real); break;
            case SIRIDB_SERIES_TP_STR:
                printf("%s\n", series->points[i].via.str); break;
            }
        }
    }
}

static void print_list(siridb_list_t * list)
{
    printf(
        "List response with %zu columns and %zu rows:\n",
        list->headers->via.array->n,
        list->data->via.array->n);
    for (size_t r = 0; r < list->data->via.array->n; r++)
    {
        qp_array_t * row = list->data->via.array->values[r].via.array;
        for (size_t c = 0; c < row->n; c++)
        {
            if (c) printf(", ");
            qp_res_fprint(row->values + c, stdout);
        }
        printf("\n");
    }
}

static void print_count(uint64_t count)
{
    printf("Count response: %" PRIu64 "\n", count);
}

static void print_calc(uint64_t calc)
{
    printf("Calc response: %lu\n", calc);
}

static void print_show(siridb_show_t * show)
{
    printf("Show response with %zu items\n", show->n);
    for (size_t i = 0; i < show->n; i++)
    {
        printf("    %s: ", show->items[i].key);
        qp_res_fprint(show->items[i].value, stdout);
        printf("\n");
    }
}

static void print_msg(const char * msg)
{
    printf("%s\n", msg);
}
