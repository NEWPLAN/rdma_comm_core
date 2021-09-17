#ifndef __RDMA_COMM_CORE_RDMA_CLIENT_SESSION_H__
#define __RDMA_COMM_CORE_RDMA_CLIENT_SESSION_H__
#include "rdma_session.h"
namespace rdma_core
{
    class RDMAClientSession : public RDMASession
    {
    public:
        RDMAClientSession(Config &conf);
        virtual ~RDMAClientSession();

    private:
        virtual void init_session(); // preparing everything for the connection

    protected: // user should re-implement this func
        virtual void lazy_config_hca();
        virtual void post_connecting();
    };
}; // end namespace rdma_core
#endif