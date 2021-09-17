/****************************************************************
 * TCPConnector is the handler of tcp connection
 * 
 * ***************************************************************/

#ifndef __RDMA_COMM_CORE_CONNECTOR_H__
#define __RDMA_COMM_CORE_CONNECTOR_H__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
namespace rdma_core
{
    class TCPConnector
    {
    public:
        TCPConnector(int, std::string, int);
        virtual ~TCPConnector()
        {
        }

    public:
        int get_or_build_socket();
        int sock_sync_data(int,
                           char *,
                           char *);
        int send_data(int, char *);
        int recv_data(int, char *);
        int recv_data_force(int, char *);

        std::string info()
        {
            std::string info_ = "TCPConnection[";
            info_ += get_my_ip() + ":" + std::to_string(get_my_port());
            info_ += "-->";
            info_ += get_peer_ip() + ":" + std::to_string(get_peer_port()) + "]";
            return info_;
        }

        static int allocate_socket(std::string info);

    public:
        std::string &get_my_ip()
        {
            return this->my_ip;
        }
        std::string &get_peer_ip()
        {
            return this->peer_ip;
        }
        int get_peer_port()
        {
            return this->peer_port;
        }
        int get_my_port()
        {
            return this->my_port;
        }

    private:
        int root_sock = 0;   // the socket handler
        std::string peer_ip; // peer ip address
        int peer_port;       //peer port
        std::string my_ip;   // my ip address
        int my_port;         // my port in use
    };

}; // end namespace rdma_core
#endif