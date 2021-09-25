#ifndef __RDMA_COMM_CORE_RDMA_SERVER_SESSION_H__
#define __RDMA_COMM_CORE_RDMA_SERVER_SESSION_H__
#include "rdma_session.h"
namespace rdma_core
{
    class RDMAServerSession : public RDMASession
    {
    public:
        explicit RDMAServerSession(Config &conf);
        virtual ~RDMAServerSession();

    protected:                       // user should re-implement this func
        virtual void init_session(); // preparing everything for the connection
        virtual void lazy_config_hca();
        virtual void post_connecting();
    };
}; //end namespace rdma_core

#endif