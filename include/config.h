#ifndef __RCL_CONFIG_H__
#define __RCL_CONFIG_H__
#include "util/net_util.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <getopt.h>

#include "util/logging.h"
#include <iostream>
#include <string.h>
#include <string>
#include <vector>

#define no_argument 0
#define required_argument 1
#define optional_argument 2

#define DEBUG_TWO_CHANNEL

// #define FULL_MESH 299
// #define SERVER_CLIENT 0

enum AllReduceType
{
    UNKNOWN_ALLREDUCE = 0,
    SERVER_CLIENT = 18,
    FULL_MESH_ALLREDUCE = 201,
    RING_ALLREDUCE = 202,
    TREE_ALLREDUCE = 203,
    DOUBLE_BINARY_TREE_ALLREDUCE = 204,
};

/* poll CQ timeout in millisec (20 milliseconds) */
#define MAX_POLL_CQ_TIMEOUT 20
#define MSG "SEND operation "
#define RDMAMSGR "RDMA read operation "
#define RDMAMSGW "RDMA write operation"

//#define MSG_BLOCK 10
#define MSG_SIZE 1000

#define SHOWN_LOG_EVERN_N 1000000

//#define DEBUG_SINGLE_THREAD

class Config
{
public:
    void usage(const char *argv0)
    {
        fprintf(stdout, "Usage:\n");
        fprintf(stdout, " %s start a server and wait for connection\n", argv0);
        fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
        fprintf(stdout, "\n");
        fprintf(stdout, "Options:\n");
        fprintf(stdout, " -p, --port <port> listen on/connect to port <port> (default 2020)\n");
        fprintf(stdout, " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
        fprintf(stdout, " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
        fprintf(stdout, " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
    }

    int parse_args(int argc, char **argv)
    {
        TRACING("Tracing parse_args");
        const struct option longopts[] = {
            {.name = "help", .has_arg = no_argument, .flag = 0, .val = 'h'},
            {.name = "event-channel", .has_arg = no_argument, .flag = 0, .val = 'e'},
            {.name = "port", .has_arg = required_argument, .flag = 0, .val = 'p'},
            {.name = "ib-dev", .has_arg = required_argument, .flag = 0, .val = 'd'},
            {.name = "ib-port", .has_arg = required_argument, .flag = 0, .val = 'i'},
            {.name = "gid-idx", .has_arg = required_argument, .flag = 0, .val = 'g'},
            {.name = "traffic-class", .has_arg = required_argument, .flag = 0, .val = 't'},
            {.name = "server", .has_arg = required_argument, .flag = 0, .val = 's'},
            {.name = "client", .has_arg = required_argument, .flag = 0, .val = 'c'},
            {.name = "cluster", .has_arg = required_argument, .flag = 0, .val = 'u'},
            {.name = "master", .has_arg = required_argument, .flag = 0, .val = 264},
            {.name = "msg-size", .has_arg = required_argument, .flag = 0, .val = 257},
            {.name = "num", .has_arg = required_argument, .flag = 0, .val = 258},
            {.name = "single-recv", .has_arg = no_argument, .flag = 0, .val = 259},
            {.name = "service-type", .has_arg = required_argument, .flag = 0, .val = 260},
            {.name = "topo", .has_arg = required_argument, .flag = 0, .val = 261},
            {.name = "tree-width", .has_arg = required_argument, .flag = 0, .val = 262},
            {.name = "role", .has_arg = required_argument, .flag = 0, .val = 263},
            {0, 0, 0, 0},
        };

        int index;
        int iarg = 0;

        // turn off getopt error message
        opterr = 1;

        while (iarg != -1)
        {
            iarg = getopt_long(argc, argv, "s:h:p:d:i:g:t:c:u:m:e:", longopts, &index);

            switch (iarg)
            {
                case 'h':
                    std::cout << "Usage:" << std::endl;
                    this->usage(argv[0]);
                    exit(0);
                    break;

                case 'v': std::cout << "You hit version" << std::endl; break;

                case 'u':
                {
                    add_to_cluster(optarg);
                    // service_type = FULL_MESH;
                    while (optind < argc && argv[optind][0] != '-')
                    {
                        add_to_cluster(argv[optind]);
                        optind++;
                    }
                }
                break;

                case 'p': tcp_port = strtoul(optarg, NULL, 0); break;
                case 'd': dev_name = strdup(optarg); break;
                case 'i':
                    ib_port = strtoul(optarg, NULL, 0);
                    if (ib_port < 0)
                    {
                        usage(argv[0]);
                        exit(-1);
                    }
                    break;
                case 'g':
                    gid_idx = strtoul(optarg, NULL, 0);
                    if (gid_idx < 0)
                    {
                        usage(argv[0]);
                        exit(-1);
                    }
                    break;
                case 's': set_server_name(optarg); break;
                case 't': traffic_class = strtoul(optarg, NULL, 0); break;
                case 'c': std::cout << "client: " << optarg << std::endl; break;
                case 'e': use_event = true; break;
                case 257: DATA_MSG_SIZE = atoi(optarg); break;

                case 258: num_senders = atoi(optarg); break;

                case 259: single_recv = true; break;

                case 261:
                {
                    std::string topology = std::string(optarg);
                    if (topology == "full-mesh")
                        all_reduce = FULL_MESH_ALLREDUCE;
                    else if (topology == "double-binary-tree" || topology == "dbt")
                        all_reduce = DOUBLE_BINARY_TREE_ALLREDUCE;
                    else if (topology == "ring")
                        all_reduce = RING_ALLREDUCE;
                    else if (topology == "tree")
                        all_reduce = TREE_ALLREDUCE;
                    else if (topology == "server-client")
                    {
                        VLOG(2) << "using topology: " << topology;
                        all_reduce = SERVER_CLIENT;
                    }
                    break;
                }
                case 262: tree_width = atoi(optarg); break;
                case 263: role = optarg; break;
                case 264: master_ip = optarg; break;
            }
        }

        conflict_check();

        return 0;
    }

    void print_config()
    {
        fprintf(stdout, " ------------------------------------------------\n");
        fprintf(stdout, " Device name : \"%s\"\n", dev_name);
        fprintf(stdout, " IB port : %u\n", ib_port);
        if (serve_as_client) { std::cout << " Serving as client, remoteIP :" << server_name; }
        else
        {
            std::cout << " Serving as Server" << std::endl;
        }
        fprintf(stdout, " TCP port : %u\n", tcp_port);
        if (gid_idx >= 0) fprintf(stdout, " GID index : %u\n", gid_idx);
        printf(" Traffic Class: %d\n", traffic_class);
        std::cout << " Using event channel: " << use_event << std::endl;
        std::cout << " MSG size: " << DATA_MSG_SIZE << "*" << MSG_BLOCK_NUM << std::endl;
        std::cout << " My IP addr: " << get_local_ip() << std::endl;
        if (cluster.size() != 0)
        {
            std::cout << " IP cluster: ";
            for (auto &each_ip : cluster) std::cout << each_ip << " ";
            std::cout << std::endl;
        }
        if (num_senders) std::cout << " Expected senders: " << num_senders << std::endl;

        std::cout << " Topology: ";
        switch (all_reduce)
        {
            case FULL_MESH_ALLREDUCE: std::cout << " Full-Mesh AllReduce" << std::endl; break;
            case TREE_ALLREDUCE: std::cout << " Tree AllReduce" << std::endl; break;
            case DOUBLE_BINARY_TREE_ALLREDUCE: std::cout << " Double-Binary-Tree AllReduce" << std::endl; break;
            case RING_ALLREDUCE: std::cout << " Ring AllReduce" << std::endl; break;
            case SERVER_CLIENT: std::cout << " Server-client" << std::endl; break;
            default: std::cout << "Unknown all reduce type" << std::endl;
        }

        if (all_reduce == TREE_ALLREDUCE) std::cout << " The Tree Allreduce width: " << tree_width << std::endl;

        std::cout << " Using single thread for receiver: " << single_recv << std::endl;
        fprintf(stdout, " ------------------------------------------------\n\n");
    }

public:
    std::string &get_local_ip()
    {
        if (local_ip.length() == 0) local_ip = NetTool::get_ip("12.12.12.111", 24);
        return local_ip;
    }

    void set_server_name(std::string ip)
    {
        std::string my_ip = get_local_ip();
        if (my_ip == ip)
        {
            server_name = ip;
            serve_as_client = false;
            LOG(WARNING) << "[invalid] You are specifying you own as the server";
            return;
        }
        serve_as_client = true;
        server_name = ip;
    }

private:
    void add_to_cluster(const char *str)
    {
        std::string tmp_ip = std::string(str);
        std::string my_ip = get_local_ip();

        if (my_ip != tmp_ip) cluster.push_back(tmp_ip);

        std::sort(cluster.begin(), cluster.end());
    }

    void conflict_check()
    {
        if (num_senders == 0 && cluster.size() != 0)
        {
            num_senders = cluster.size();
            LOG(INFO) << "The communication width is: " << num_senders;
        }

        bool continue_check = true;
        if (single_recv && !num_senders)
        {
            LOG(WARNING)
                << "You must explicitly speficy the # of senders when using the single thread model to recv data";
            continue_check = false;
        }
        // if (continue_check && serve_as_client && cluster.size() != 0)
        // {
        //     LOG(WARNING) << "You cannot enable Full-Mesh and 1:N modes together";
        //     continue_check = false;
        // }

        if (continue_check && all_reduce == TREE_ALLREDUCE && tree_width <= 0)
        {
            LOG(WARNING) << "In tree allreduce mode, tree_width(" << tree_width << ") should be > 0.";
            continue_check = false;
        }

        if (continue_check && all_reduce == SERVER_CLIENT)
        {
            if (server_name == "" && serve_as_client == true) LOG(FATAL) << "You mush speficy master node";
            std::string this_ip = get_local_ip();
            if (this_ip == server_name)
                serve_as_client = false;
            else
                serve_as_client = true;
        }
        // print_config();

        if (!continue_check)
        {
            print_config();
            exit(-1);
        }
    }

public:
    const char *dev_name = "mlx5_0";                   /* IB device name */
    std::string server_name = "";                      /* server host name */
    bool serve_as_client = false;                      /*run as a client, otherwise serve as server*/
    uint32_t tcp_port = 2020;                          /* server TCP port */
    int ib_port = 1;                                   /* local IB port to work with */
    int gid_idx = 3;                                   /* gid index to use */
    uint32_t traffic_class = 0;                        /* traffic class*/
    bool use_event = false;                            /*If use completion channel (event driven)*/
    int DATA_MSG_SIZE = 1000;                          /*default msg for DATA CHANNEL 10K*/
    int CTRL_MSG_SIZE = 4;                             /*default msg for ctrl channel 16 bytes*/
    int MSG_BLOCK_NUM = 2;                             /*default msg blk*/
    std::string local_ip = "";                         /* local IP*/
    std::vector<std::string> cluster;                  /*cluster IP addr*/
    int num_senders = 0;                               /*num senders for this node*/
    bool single_recv = false;                          /*using single receiver thread for all poll cq*/
    int service_type = 0;                              /*service type: Full_Mesh, Server_Cient*/
    enum AllReduceType all_reduce = UNKNOWN_ALLREDUCE; /*Allreduce type, support for tree, ring,and full_mesh*/
    int tree_width = 0;                                /*the tree allreduce width*/
    std::vector<std::vector<std::string>> sub_groups;  /*allreduce group for tree*/

    std::string role;
    std::string master_ip; // master node addr
};
#endif
