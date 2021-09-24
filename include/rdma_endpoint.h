/**********************************************************************
 * RDMAEndPoint is the handler of an connection for RDMA communication
 * it owns two subhandlers, i.e., 
 *      --- TCPConnector: the tcp connection abstraction
 *      --- RDMAChannel: the RDMA communication abstraction
 * ********************************************************************/

#ifndef __RDMA_COMM_CORE_RDMA_END_POINT_H__
#define __RDMA_COMM_CORE_RDMA_END_POINT_H__
#include "config.h"
#include "rdma_channel.h"
#include "rdma_session.h"
#include "tcp_connector.h"
#include <memory>
#include <string>
#include "rdma_config.h"
namespace rdma_core
{
    class RDMAChannel;
    class RDMASession;

    class RDMAEndPoint
    {
    public:
        virtual ~RDMAEndPoint();

    protected:
        explicit RDMAEndPoint(     // the construction function
            int fd,                // the pre_connector socket
            std::string ip,        // the peer id_addr
            int port,              //the peer port
            Config &conf,          // work env
            RDMASession *session); // registered session

    public:
        inline static RDMAEndPoint *create_endpoint( // a static function to build the new endpoint function
            int fd,                                  // the pre_connector socket
            std::string ip,                          // the peer id_addr
            int port,                                //the peer port
            Config &conf,                            // work env
            RDMASession *session)
        {
            return new RDMAEndPoint(fd, ip, port, conf, session);
        }

        //virtual void loading();

        void sync_with_peer(std::string info = ""); //using TCP to sync with remote

        inline std::string info(); // return the detail information of this endpoint

        inline RDMASession *get_registered_session() // get the session of this endpoint registered in
        {
            return registered_session_;
        }

        void connecting(); // connecting to peer

        std::string get_id(); // return the id of this endpoint

        inline RDMAChannel *get_channel(uint32_t index = 0) // fetch the rdmachannel for this connection
        {
            CHECK(index == 0) << "Currently, we only support one channel in a endpoint";
            return rdma_channel_mgr_[index].get();
        }
        void setup_index_in_session(int index);

        void reset();

    private:
        std::string get_unique_id();                // build a unique id for the rdma endpoint
        void setup_rdma_channel(std::string info_); // build an rdma_channel

    private:
        std::string id_ = "RDMAEndPoint";                            // the id of the endpoint
        Config work_env_;                                            // work env for params
        std::unique_ptr<TCPConnector> pre_connector_;                // a helper to setting up the rdma_channel
        std::vector<std::unique_ptr<RDMAChannel>> rdma_channel_mgr_; // a manager of RDMAChannel
        RDMASession *registered_session_ = nullptr;                  // the session of this endpoint registered in
    };

}; // end namespace rdma_core
#endif