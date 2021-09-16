#include <infiniband/verbs.h>
#include <rdma/rdma_verbs.h>

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "echo.h"

static void on_recv_completion(struct ibv_wc* wc)
{
    if (wc->status != IBV_WC_SUCCESS) {
        printf("Completion failed!");
        return;
    }

    // receive
    connection_t* conn = (connection_t*)(uintptr_t)wc->wr_id;
    if (wc->opcode & IBV_WC_RECV) {
        printf("Echo back: %s\n", conn->recv_buf);
        // then echo...
    } else if (wc->opcode == IBV_WC_SEND) {
        printf("Echo result: %s.\n", conn->send_buf);
    }

    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge = {
        .addr = (uintptr_t)conn->send_buf,
        .length = BUFFER_SIZE,
        .lkey = conn->send_mr->lkey
    };

    memset(&wr, 0, sizeof(wr));
    wr.opcode = IBV_WR_SEND;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr_id = wc->wr_id;

    TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}

static int on_cm_connection_request(struct rdma_cm_id* id)
{
    struct rdma_conn_param conn_param;
    printf("Connection request from %s.\n", get_inet_peer_address(id));

    // initialize app context if not initialized, build peer connection
    // create queue pair, register memory, initialize memory buffers,
    // and post initial receives
    initialize_peer_connection(id, on_recv_completion);

    // accept connection
    memset(&conn_param, 0, sizeof(struct rdma_conn_param));
    TEST_NZ(rdma_accept(id, &conn_param));
    return 0;
}

static int on_cm_connection_established(struct rdma_cm_id* id)
{
    connection_t* conn = (connection_t*)id->context;
    printf("%s Connected! Send Q: %s | Receive Q: %s\n",
        get_inet_peer_address(id), conn->send_buf, conn->recv_buf);
    return 0;
}

static int on_cm_connection_disconnect(struct rdma_cm_id* id)
{
    destroy_peer_context(id);
    return 0;
}

static int on_cm_event(struct rdma_cm_event* event)
{
    switch (event->event) {
    case RDMA_CM_EVENT_CONNECT_REQUEST:
        return on_cm_connection_request(event->id);
    case RDMA_CM_EVENT_ESTABLISHED:
        return on_cm_connection_established(event->id);
    case RDMA_CM_EVENT_DISCONNECTED:
        return on_cm_connection_disconnect(event->id);
    default:
        return -1;
    }
}

int main(int argc, char* argv[])
{
    struct rdma_cm_id* cm_id = NULL;
    struct rdma_event_channel* cm_eventchannel = rdma_create_event_channel();
    struct rdma_cm_event *cm_event = NULL, cm_event_buffer;
    struct sockaddr_in sockaddr;

    if (argc != 3) {
        printf("usage: %s <ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(&sockaddr, 0, sizeof(struct sockaddr_in));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(atoi(argv[2]));

    // create connection manager id
    TEST_NZ(rdma_create_id(cm_eventchannel, &cm_id, NULL, RDMA_PS_TCP));
    // bind to port and socket
    TEST_NZ(rdma_bind_addr(cm_id, (struct sockaddr*)&sockaddr));
    // start listening
    TEST_NZ(rdma_listen(cm_id, 10));
    uint16_t port = ntohs(sockaddr.sin_port);
    uint16_t rdma_port = ntohs(rdma_get_src_port(cm_id));
    printf("Listening to port %d\n", rdma_port);
    printf("addrinfo port %d == %d rdma_get_src_port\n", port, rdma_port);

    // check events on the connection manager
    while (rdma_get_cm_event(cm_eventchannel, &cm_event) == 0) {
        // copy connection manager event to a buffer
        memcpy(&cm_event_buffer, cm_event, sizeof(struct rdma_cm_event));
        // acknowledge and free the event
        rdma_ack_cm_event(cm_event);

        // process the connection manager event if connection established then break
        if (on_cm_event(&cm_event_buffer) >= 0) {
            break;
        }
    }

    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_eventchannel);
    return 0;
}