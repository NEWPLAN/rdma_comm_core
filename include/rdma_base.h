#ifndef __SCHEDULING_RDMA_BASE_H__
#define __SCHEDULING_RDMA_BASE_H__

#include "util/logging.h"
#include <arpa/inet.h>
#include <byteswap.h>
#include <endian.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace rdma_core
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    static inline uint64_t
    htonll(uint64_t x)
    {
        return bswap_64(x);
    }
    static inline uint64_t ntohll(uint64_t x)
    {
        return bswap_64(x);
    }
#elif __BYTE_ORDER == __BIG_ENDIAN
    static inline uint64_t
    htonll(uint64_t x)
    {
        return x;
    }
    static inline uint64_t ntohll(uint64_t x)
    {
        return x;
    }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

}; // namespace rdma_core

#endif