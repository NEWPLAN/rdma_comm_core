#include "rdma_client_sess.h"
#include "rdma_buffer.h"
#include "rdma_session.h"

#include <chrono>
#include <thread>

#include "tcp_connector.h"
namespace rdma_core
{
    RDMAClientSession::RDMAClientSession(Config &conf) :
        RDMASession(conf)
    {
        TRACE_IN;
        session_id_ = "ConnectionTest";
        VLOG(3) << "Creating a new RDMAClientSession for " << session_id_;
        TRACE_OUT;
    }
    RDMAClientSession::~RDMAClientSession()
    {
        TRACE_IN;
        VLOG(3) << "Destroying the RDMAClientSession of " << session_id_;
        TRACE_OUT;
    }

    void RDMAClientSession::init_session()
    {
        TRACE_IN;
        int con_fd = TCPConnector::allocate_socket("RDMAClientSession");
        {
            VLOG(2) << "RDMAClient has prepared everything for Connecting ...";
            struct sockaddr_in c_to_server;
            c_to_server.sin_family = AF_INET;
            c_to_server.sin_port = htons(this->work_env_.tcp_port);
            c_to_server.sin_addr.s_addr = inet_addr(work_env_.master_ip.c_str());
            int count_try = 10 * 300; //default 300s
            do
            {
                if (connect(con_fd, (struct sockaddr *)&c_to_server,
                            sizeof(c_to_server))
                    == 0)
                {
                    is_connected_ = true;
                    break; // break when successing
                }

                LOG_EVERY_N(INFO, 10) << "[" << count_try / 10
                                      << "] Failed to connect: "
                                      << work_env_.master_ip
                                      << ":" << this->work_env_.tcp_port;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (count_try-- > 0);
            accept_new_connection(con_fd, c_to_server);
            LOG(INFO) << "[Done] Connecting ...";
        }
        TRACE_OUT;
    }

    void RDMAClientSession::lazy_config_hca()
    {
        TRACE_IN;
        VLOG(3) << "RDMAClientSession is reconfiguring the channel&adapter";
        for (auto &each_endpoint : end_point_mgr_)
        {
            auto &a_config = each_endpoint->get_channel()->get_config();
            a_config.using_shared_cq = false;
            //a_config.traffic_class = 64;
        }
        TRACE_OUT;
    }
    void RDMAClientSession::post_connecting()
    {
        TRACE_IN;
        VLOG(3) << "RDMAClientSession is doing when post_connecting";
        TRACE_OUT;
    }

}; // end namespace rdma_core