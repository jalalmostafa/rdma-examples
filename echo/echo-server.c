#include <infiniband/verbs.h>
#include <rdma/rdma_verbs.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VERB_ERR(verb, ret) \
    fprintf(stderr, "%s returned %d errno %d: %s\n", verb, ret, errno, strerror(errno))

#define VERB_OUT(verb, msg) \ 
    fprintf(stdout, "%s: %s\n", verb, msg)

#define DEFAULT_PORT "51216"
#define DEFAULT_MSG_COUNT 100
#define DEFAULT_MSG_LENGTH 1000000
#define DEFAULT_MSEC_DELAY 500

typedef struct context {
    int a;
} context_t;

int main(int argc, char* argv[])
{
    struct rdma_cm_id* cm_id = NULL;
    struct rdma_event_channel* event_channel = rdma_create_event_channel();
    struct rdma_addrinfo *rai = NULL, hints;
    struct sockaddr_in boundSockAddr;

    memset(&hints, 0, sizeof(hints));
    hints.ai_port_space = RDMA_PS_IB;
    hints.ai_flags = RAI_PASSIVE;

    printf("rdma_getaddrinfo %s:%s\n", argv[0], argv[1]);
    int ret = rdma_getaddrinfo(argv[0], argv[1], &hints, &rai);
    if (ret) {
        VERB_ERR("rdma_getaddrinfo", ret);
        return ret;
    }

    ret = rdma_create_id(event_channel, &cm_id, NULL, RDMA_PS_TCP);
    if (ret) {
        VERB_ERR("rdma_create_id", ret);
        return ret;
    }

    ret = rdma_bind_addr(cm_id, (struct sockaddr*)&boundSockAddr);
    if (ret) {
        VERB_ERR("rdma_bind_addr", ret);
        return ret;
    }

    ret = rdma_listen(cm_id, 10);
    if (ret) {
        VERB_ERR("rdma_listen", ret);
        return ret;
    } else {
        uint16_t port = ntohs(boundSockAddr.sin_port);
        printf("Listening to port %d\n", port);
    }

    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.cap.max_send_wr = 1;
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(event_channel);
    rdma_freeaddrinfo(rai);
    return 0;
}