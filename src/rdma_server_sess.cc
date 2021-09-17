
#include "rdma_server_sess.h"
#include "rdma_buffer.h"
#include "rdma_session.h"

#include "tcp_connector.h"

namespace rdma_core
{
    RDMAServerSession::RDMAServerSession(Config &conf) :
        RDMASession(conf)
    {
        TRACE_IN;
        session_id_ = "ConnectionTest";
        VLOG(3) << "Creating a new RDMAServerSession for " << session_id_;
        TRACE_OUT;
    }
    RDMAServerSession::~RDMAServerSession()
    {
        TRACE_IN;
        VLOG(3) << "Destroying the RDMAServerSession of " << info();
        TRACE_OUT;
    }

    void RDMAServerSession::init_session()
    {
        TRACE_IN;
        int listen_fd = TCPConnector::allocate_socket("RDMAServerSession");
        VLOG(1) << "RDMAServer is preparing everything for accepting new connections";
        {
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = inet_addr("0.0.0.0");
            sin.sin_port = htons(work_env_.tcp_port);

            if (bind(listen_fd, (struct sockaddr *)&sin,
                     sizeof(sin))
                < 0)

                LOG(FATAL) << "Error when binding to socket";

            if (listen(listen_fd, 1024) < 0)
                LOG(FATAL) << "Error when listen on socket";

            VLOG(2) << "The server is listening on " << work_env_.tcp_port;
            uint32_t connected = 0;

            do //loop wait and build new rdma connections
            {
                struct sockaddr_in cin;
                socklen_t len = sizeof(cin);
                int client_fd;

                if ((client_fd = accept(listen_fd, (struct sockaddr *)&cin, &len)) == -1)
                    LOG(FATAL) << "Error of accepting new connection";

                accept_new_connection(client_fd, cin);

                connected++;
                VLOG(3) << "received connection: " << connected << "/" << work_env_.cluster.size();

                if (connected >= work_env_.cluster.size())
                    break;

            } while (true);
            is_connected_ = true;
        }
        TRACE_OUT;
    }

    void RDMAServerSession::lazy_config_hca()
    {
        VLOG(3) << "RDMAServerSession is reconfiging the channel&adapter";
        for (auto &each_endpoint : end_point_mgr_)
        {
            auto &a_config = each_endpoint->get_channel()->get_config();
            a_config.using_shared_cq = true;
            a_config.cq_key = "SharedCQ@" + info();
            //a_config.traffic_class = 64;
        }
    }
    void RDMAServerSession::post_connecting()
    {
        VLOG(3) << "RDMAServerSession is handling with post_connecting";
    }

}; // end namespace rdma_core