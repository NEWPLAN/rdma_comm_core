/****************************************************************
 * RDMASession is the base of RDMA Communication workflow.
 * It would derive two typical sub-session to handle the workflow 
 * of different roles.
 *      -- ClientSession for 'Client side'
 *      -- ServerSession for "Server side"
 * ***************************************************************/

#ifndef __RDMA_COMM_CORE_RDMA_SESSION_H__
#define __RDMA_COMM_CORE_RDMA_SESSION_H__
#include "config.h"
#include "rdma_channel.h"
#include "rdma_endpoint.h"
#include <functional>
#include <vector>
#include "rdma_config.h"

namespace rdma_core
{
    class RDMAChannel;
    class RDMAEndPoint;

    class RDMASession
    {
    public:
        explicit RDMASession(Config &conf); // construction function
        virtual ~RDMASession();             // destroy function
        virtual inline std::string info()   //show the info of the session
        {
            return work_env_.role + "@" + session_id_;
        }

        virtual std::string get_id() // return the unique id of this session
        {
            return work_env_.role + "@" + session_id_;
        }

        virtual void init_session() = 0; // preparing everything for the connection
        virtual void running();          // start services

        bool register_event_callback(                                                            // register a group of callback functions
            std::function<void(std::vector<RDMAChannel *> g_channels)> established_done,         // when establish is done before start rdma services
            std::function<void(struct ibv_wc *wc, void *args)> process_send_done,                // process when send data done
            std::function<void(struct ibv_wc *wc, void *args)> process_recv_done,                // process when recv data done
            std::function<void(struct ibv_wc *wc, void *args)> process_recv_write_with_imm_done, // process when recv data done
            std::function<void(struct ibv_wc *wc, void *args)> process_write_done,               // process when write data done
            std::function<void(struct ibv_wc *wc, void *args)> process_read_done);               // process when read data done

        void connecting(); // real connecting for each endpoint

    protected:
        virtual void process_CQ(std::vector<RDMAChannel *> aggregated_channels); // a loop to processing complement queue events
        virtual void lazy_config_hca() = 0;                                      // hook before the connecting endpoint
        virtual void post_connecting() = 0;                                      // hook after endpoint is connected

    protected:
        // accept for new connection
        virtual int accept_new_connection(int sock_fd,             // socket_fd for new connection
                                          struct sockaddr_in cin); // socket address for new connection

        virtual void default_established_fn(std::vector<RDMAChannel *> g_channels);
        virtual void default_process_send_done(struct ibv_wc *wc, void *args);
        virtual void default_process_recv_done(struct ibv_wc *wc, void *args);
        virtual void default_process_write_done(struct ibv_wc *wc, void *args);
        virtual void default_process_read_done(struct ibv_wc *wc, void *args);
        virtual void default_process_recv_write_with_imm_done(struct ibv_wc *wc, void *args);
        void real_connecting();

    protected:
        Config work_env_;                                          // environment of this session
        std::string session_id_ = "BaseSession";                   // identify of the object
        std::vector<std::unique_ptr<RDMAEndPoint>> end_point_mgr_; // a manager of rdmaendpoint
        std::function<void()> before_processing_cb_;               // callback when connection is established
        bool is_connected_ = false;                                // the connection has been set up
        int socket_fd = 0;                                         //socket for connection

        // event processing functions
        std::function<void(std::vector<RDMAChannel *> g_channels)> established_done_ = nullptr;         // establish is done before start rdma services
        std::function<void(struct ibv_wc *wc, void *args)> process_send_done_ = nullptr;                // process when send data done
        std::function<void(struct ibv_wc *wc, void *args)> process_recv_done_ = nullptr;                // process when recv data done
        std::function<void(struct ibv_wc *wc, void *args)> process_recv_write_with_imm_done_ = nullptr; // process when recv_write_with_imm done
        std::function<void(struct ibv_wc *wc, void *args)> process_write_done_ = nullptr;               // process when write data done
        std::function<void(struct ibv_wc *wc, void *args)> process_read_done_ = nullptr;                // process when read data done
    };

}; // end namespace rdma_core
#endif