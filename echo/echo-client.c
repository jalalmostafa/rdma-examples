#include <infiniband/verbs.h>
#include <rdma/rdma_verbs.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

#include "echo.h"

const int timeout = 500;

static void on_complete(struct ibv_wc* wc)
{
    if (wc->status != IBV_WC_SUCCESS) {
        printf("Completion failed!");
        return;
    }

    connection_t* conn = (connection_t*)(uintptr_t)wc->wr_id;
    if (wc->opcode & IBV_WC_RECV) {
        printf("[%lu] Received: %s\n", wc->wr_id, conn->recv_buf);
    } else if (wc->opcode == IBV_WC_SEND) {
        printf("[%lu] Sent: %s\n", wc->wr_id, conn->send_buf);
    }
}

static int on_addr_resolved(struct rdma_cm_id* id)
{

    printf("Address resolved to: %s! Resolving Route...\n", get_inet_peer_address(id));

    // initialize app context if not initialized, build peer connection
    // create queue pair, register memory, initialize memory buffers,
    // and post initial receives
    initialize_peer_connection(id, on_complete);

    // resolve route to the server address
    TEST_NZ(rdma_resolve_route(id, timeout));
    return 0;
}

static int on_route_resolved(struct rdma_cm_id* id)
{
    struct rdma_conn_param conn_param;
    printf("Route to %s resolved! Connecting...", get_inet_peer_address(id));
    TEST_NZ(rdma_connect(id, &conn_param));
    return 0;
}

static int on_connection(struct rdma_cm_id* id)
{
    char buffer[BUFFER_SIZE];
    int exit_flag = 0;

    connection_t* conn = (connection_t*)id->context;

    do {
        struct ibv_send_wr send_wr, *bad_send_wr = NULL;
        struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
        struct ibv_sge send_sge = { .addr = (uintptr_t)conn->send_buf, .length = BUFFER_SIZE, .lkey = conn->send_mr->lkey };
        struct ibv_sge recv_sge = { .addr = (uintptr_t)conn->recv_buf, .length = BUFFER_SIZE, .lkey = conn->recv_mr->lkey };

        memset(buffer, 0, BUFFER_SIZE);

        scanf(">>>%s\n", buffer);

        if (strcmp("exit", buffer) != 0) {
            memcpy(conn->send_buf, buffer, BUFFER_SIZE);

            send_wr.next = NULL;
            send_wr.num_sge = 1;
            send_wr.sg_list = &send_sge;
            send_wr.send_flags = IBV_SEND_SIGNALED;
            send_wr.opcode = IBV_WR_SEND;
            send_wr.wr_id = (uintptr_t)conn;
            TEST_NZ(ibv_post_send(conn->qp, &send_wr, &bad_send_wr));

            recv_wr.next = NULL;
            recv_wr.num_sge = 1;
            recv_wr.sg_list = &recv_sge;
            recv_wr.wr_id = (uintptr_t)conn;

            TEST_NZ(ibv_post_recv(conn->qp, &recv_wr, &bad_recv_wr));
        } else {
            exit_flag = 1;
        }
    } while (!exit_flag);
    return 0;
}

static int on_disconnection(struct rdma_cm_id* id)
{
    destroy_peer_context(id);
    return 0;
}

static int connection_event(struct rdma_cm_event* event)
{
    switch (event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:
        return on_addr_resolved(event->id);
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
        return on_route_resolved(event->id);
    case RDMA_CM_EVENT_ESTABLISHED:
        return on_connection(event->id);
    case RDMA_CM_EVENT_DISCONNECTED:
        return on_disconnection(event->id);
    default:
        return EXIT_FAILURE;
    }
}

int main(int argc, char* argv[])
{
    struct addrinfo* addr;
    struct rdma_cm_id* cm_id = NULL;
    struct rdma_event_channel* channel = NULL;
    struct rdma_cm_event* event = NULL;

    if (argc != 3) {
        printf("usage: %s <ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    TEST_NZ(getaddrinfo(argv[1], argv[2], NULL, &addr));
    TEST_Z(channel = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(channel, &cm_id, NULL, RDMA_PS_TCP));
    printf("Trying to connect to: %s\n", argv[1]);
    TEST_NZ(rdma_resolve_addr(cm_id, NULL, addr->ai_addr, timeout));
    freeaddrinfo(addr);

    while (rdma_get_cm_event(channel, &event)) {
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(struct rdma_cm_event));
        rdma_ack_cm_event(event);
        if (connection_event(&event_copy)) {
            break;
        }
    }

    rdma_destroy_event_channel(channel);
    rdma_destroy_id(cm_id);
    return 0;
}
