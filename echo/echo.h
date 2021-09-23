#ifndef ECHO_H
#define ECHO_H

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_verbs.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024

#define TEST_NZ(x)                                                          \
    do {                                                                    \
        if ((x)) {                                                          \
            fprintf(stderr, "error: " #x ": %d %s.\n", errno, strerror(errno)); \
            exit(errno);                                                    \
        }                                                                   \
    } while (0)

#define TEST_Z(x)                                                           \
    do {                                                                    \
        if (!(x)) {                                                         \
            fprintf(stderr, "error: " #x ": %d %s.\n", errno, strerror(errno)); \
            exit(errno);                                                    \
        }                                                                   \
    } while (0)

typedef struct connection {
    struct ibv_qp* qp;
    struct ibv_mr* send_mr;
    struct ibv_mr* recv_mr;
    char* send_buf;
    char* recv_buf;
} connection_t;

typedef struct app_context {
    struct ibv_context* verbs;
    struct ibv_pd* protectionDomain;
    struct ibv_cq* completionQ;
    struct ibv_comp_channel* channel;
    pthread_t cq_poller_thread;
} app_context_t;

typedef void (*on_complete_t)(struct ibv_wc*);

app_context_t* app_context = NULL;

static void* pollcq(void* poncomplete)
{
    struct ibv_cq* cq;
    struct ibv_wc wc;
    void* ctx = NULL;
    on_complete_t on_complete = (on_complete_t)poncomplete;
    while (1) {
        TEST_NZ(ibv_get_cq_event(app_context->channel, &cq, &ctx));
        ibv_ack_cq_events(cq, 0);
        TEST_NZ(ibv_req_notify_cq(cq, 0));
        while (ibv_poll_cq(cq, 1, &wc)) {
            on_complete(&wc);
        }
    }
}

static void build_app_context(struct ibv_context* verbs_context, on_complete_t on_complete)
{
    if (app_context != NULL) {
        if (app_context->verbs != verbs_context) {
            printf("Multiple Contexts?! Different Devices?!\n");
            exit(EXIT_FAILURE);
        }

        return;
    }

    app_context = (app_context_t*)calloc(1, sizeof(app_context_t));
    app_context->verbs = verbs_context;

    // Allocate Protection Domain - returns NULL on failure
    TEST_Z(app_context->protectionDomain = ibv_alloc_pd(verbs_context));

    // Create Completion Events Channel
    TEST_Z(app_context->channel = ibv_create_comp_channel(verbs_context));

    // Create Completion Queue
    TEST_Z(app_context->completionQ = ibv_create_cq(verbs_context, 10, NULL, app_context->channel, 0));

    // Start receiving completion queue notifications
    TEST_NZ(ibv_req_notify_cq(app_context->completionQ, 0));
    TEST_NZ(pthread_create(&app_context->cq_poller_thread, NULL, pollcq, on_complete));
}

void initialize_peer_connection(struct rdma_cm_id* id, on_complete_t on_complete)
{
    struct ibv_qp_init_attr qp_attr;

    connection_t* connection = NULL;
    build_app_context(id->verbs, on_complete);
    id->context = connection = (connection_t*)malloc(sizeof(connection_t));

    // create queue pair with its attributes
    memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_attr.recv_cq = app_context->completionQ;
    qp_attr.send_cq = app_context->completionQ;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_recv_wr = qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_sge = qp_attr.cap.max_send_sge = 1;

    TEST_NZ(rdma_create_qp(id, app_context->protectionDomain, &qp_attr));
    connection->qp = id->qp;

    // Initialize memory buffers and register them
    connection->recv_buf = (char*)calloc(1, BUFFER_SIZE);
    connection->send_buf = (char*)calloc(1, BUFFER_SIZE);
    TEST_Z(connection->recv_mr = ibv_reg_mr(app_context->protectionDomain,
               connection->recv_buf, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    TEST_Z(connection->send_mr = ibv_reg_mr(app_context->protectionDomain,
               connection->send_buf, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));

    // post initial receives
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge = {
        .addr = (uintptr_t)connection->recv_buf,
        .length = BUFFER_SIZE,
        .lkey = connection->recv_mr->lkey
    };
    wr.sg_list = &sge;
    wr.wr_id = (uintptr_t)connection;
    wr.next = NULL;
    wr.num_sge = 1;

    TEST_NZ(ibv_post_recv(connection->qp, &wr, &bad_wr));
}

char* get_inet_peer_address(struct rdma_cm_id* id)
{
    struct sockaddr_in* addr = (struct sockaddr_in*)rdma_get_peer_addr(id);
    return inet_ntoa(addr->sin_addr);
}

void destroy_peer_context(struct rdma_cm_id* id)
{
    connection_t* conn = (connection_t*)id->context;
    rdma_destroy_qp(id);
    rdma_dereg_mr(conn->recv_mr);
    rdma_dereg_mr(conn->send_mr);
    free(conn->recv_buf);
    free(conn->send_buf);
    rdma_destroy_id(id);
    free(conn);
}

#endif