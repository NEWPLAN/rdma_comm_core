#ifndef __RDMA_COMM_CORE_RDMA_CONFIG_H__
#define __RDMA_COMM_CORE_RDMA_CONFIG_H__
#include <vector>
#include <string>

namespace rdma_core
{
#define SHOWN_LOG_EVERN_N 1000000

    struct Config
    {
        const char *dev_name = "mlx5_0";                  /* IB device name */
        std::string server_name = "";                     /* server host name */
        bool serve_as_client = false;                     /*run as a client, otherwise serve as server*/
        uint32_t tcp_port = 2020;                         /* server TCP port */
        int ib_port = 1;                                  /* local IB port to work with */
        int gid_idx = 3;                                  /* gid index to use */
        uint32_t traffic_class = 0;                       /* traffic class*/
        bool use_event = false;                           /*If use completion channel (event driven)*/
        int DATA_MSG_SIZE = 1000;                         /*default msg for DATA CHANNEL 10K*/
        int CTRL_MSG_SIZE = 4;                            /*default msg for ctrl channel 16 bytes*/
        int MSG_BLOCK_NUM = 2;                            /*default msg blk*/
        std::string local_ip = "";                        /* local IP*/
        std::vector<std::string> cluster;                 /*cluster IP addr*/
        int num_senders = 0;                              /*num senders for this node*/
        bool single_recv = false;                         /*using single receiver thread for all poll cq*/
        int service_type = 0;                             /*service type: Full_Mesh, Server_Cient*/
        int tree_width = 0;                               /*the tree allreduce width*/
        std::vector<std::vector<std::string>> sub_groups; /*allreduce group for tree*/
        std::string role;                                 /* which role to used*/
        std::string master_ip;                            /* master node addr*/
        std::string session_id;                           /*session id*/
    };
}; // namespace rdma_core

#endif //__RDMA_COMM_CORE_RDMA_CONFIG_H__