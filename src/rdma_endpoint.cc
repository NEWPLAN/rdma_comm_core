#include "rdma_endpoint.h"
#include "rdma_channel.h"
#include "rdma_config.h"

namespace rdma_core
{
    RDMAEndPoint::~RDMAEndPoint()
    {
        //UNIMPLEMENTED;
        VLOG(3) << "Destroy RDMAEndPoint ";
    }

    RDMAEndPoint::RDMAEndPoint(int fd,               // the pre_connector socket
                               std::string ip,       // the peer id_addr
                               int port,             //the peer port
                               Config &conf,         // work env
                               RDMASession *session) // registered in session
        :
        work_env_(conf),
        registered_session_(session)
    {
        TRACE_IN;
        VLOG(3) << "Create RDMAEndPoint";

        CHECK(pre_connector_ == nullptr) << "tcp connector must be null";
        pre_connector_.reset(new TCPConnector(fd, ip, port));
        id_ = get_unique_id();
        this->sync_with_peer("Test Connection for TCPConnector");
        VLOG(3) << "Connection Test OK for TCPConnector"
                << "@" << this->info();
        setup_rdma_channel("DataChannel");
        TRACE_OUT;
    }

    void RDMAEndPoint::sync_with_peer(std::string info_)
    {
        TRACE_IN;

        char temp_char;
        if (pre_connector_->sock_sync_data(1, (char *)"Q", &temp_char))
            LOG(FATAL) << "sync error for " << info_;

        TRACE_OUT;
    }

    void RDMAEndPoint::connecting()
    {
        for (auto &channel : rdma_channel_mgr_)
        { // now, we are connecting to remote
            // AdapterConfig adapt_conf;
            // // adapt_conf.cq_key = "SharedGlobalCQ";
            // adapt_conf.using_shared_cq = false;
            auto adapt_info = channel->loading();
            AdapterInfo peer_info;
            pre_connector_->sock_sync_data(sizeof(peer_info),
                                           (char *)&adapt_info,
                                           (char *)&peer_info);
            channel->connecting(peer_info);
            {
                char temp_char;
                if (pre_connector_->sock_sync_data(1, (char *)"Q", &temp_char))
                    LOG(FATAL) << "sync error after QPs are connected";
            }
        }
    }

    void RDMAEndPoint::reset()
    {
        VLOG(3) << "Would reset the RDMAEndPoint";
        for (auto &channel : rdma_channel_mgr_)
        {
            channel->reset_hca();
        }
        this->connecting();
    }

    std::string RDMAEndPoint::get_unique_id()
    {
        CHECK(pre_connector_ != nullptr) << "TCPConnector is not initialized";
        return "RDMAEndPoint(" + pre_connector_->get_my_ip() + ":" + std::to_string(pre_connector_->get_my_port()) + "-->" + pre_connector_->get_peer_ip() + ":" + std ::to_string(pre_connector_->get_peer_port()) + ")";
    }

    std::string RDMAEndPoint::get_id()
    {
        CHECK(pre_connector_ != nullptr) << "RDMAEndPoint is not initialized before using";
        return "[" + pre_connector_->get_my_ip() + ":" + std::to_string(pre_connector_->get_my_port()) + "-->" + pre_connector_->get_peer_ip() + ":" + std ::to_string(pre_connector_->get_peer_port()) + "]@" + registered_session_->get_id();
    }

    void RDMAEndPoint::setup_rdma_channel(std::string info_) // build an rdma_channel
    {
        VLOG(3) << "creating&preparing an rdma channel for " << info_;
        RDMAChannel *channel = RDMAChannel::build_rdma_channel(work_env_, info_, this);
        rdma_channel_mgr_.push_back(std::move(std::unique_ptr<RDMAChannel>(channel)));
    }

    void RDMAEndPoint::setup_index_in_session(int index)
    {
        rdma_channel_mgr_[0]->setup_index_in_session(index);
    }

    std::string RDMAEndPoint::info() // return the detail information of this endpoint
    {
        return "RDMAEndPoint@" + get_id();
    }

}; // end namespace rdma_core