#include "tcp_connector.h"
#include "util/ip_qos_helper.h"
#include "util/logging.h"

#include <arpa/inet.h>
#include <linux/ip.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rdma_core
{
    TCPConnector::TCPConnector(int sock, std::string ip, int port)
    {
        this->root_sock = sock;
        this->peer_ip = ip;
        this->peer_port = port;
        {
            struct sockaddr_in serv;
            char serv_ip[20];
            socklen_t serv_len = sizeof(serv);
            getsockname(sock, (struct sockaddr *)&serv, &serv_len);
            inet_ntop(AF_INET, &serv.sin_addr, serv_ip, sizeof(serv_ip));
            this->my_ip = serv_ip;
            this->my_port = ntohs(serv.sin_port);
        }
    }

    int TCPConnector::get_or_build_socket()
    {
        if (this->root_sock == 0)
        {
            this->root_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (this->root_sock <= 0)
                LOG(FATAL) << "Error of creating socket...";
            LOG(INFO) << "Creating root socket: " << this->root_sock;
        }
        return this->root_sock;
    }
    int TCPConnector::sock_sync_data(int xfer_size,
                                     char *local_data,
                                     char *remote_data)
    {
        int write_msg, recv_msg;

        write_msg = send_data(xfer_size, local_data);
        recv_msg = recv_data_force(xfer_size, remote_data);

        //LOG(INFO) << "Recv_msg: " << recv_msg << ", write msg_" << write_msg;

        return recv_msg - write_msg;
    }

    int TCPConnector::send_data(int msg_size, char *data_placeholder)
    {
        int sock = get_or_build_socket();
        //int write_ret = write(sock, data_placeholder, msg_size);
        int write_ret = send(sock, data_placeholder, msg_size, 0);
        if (write_ret < msg_size)
            LOG(FATAL) << "Failed writing data to remote";
        return write_ret;
    }
    int TCPConnector::recv_data(int msg_size, char *data_placeholder)
    {
        int sock = get_or_build_socket();
        int rc = 0;
        int total_read_bytes = 0;

        while (!rc && total_read_bytes < msg_size)
        {
            //int read_bytes = read(sock, data_placeholder, msg_size);
            int read_bytes = recv(sock, data_placeholder + total_read_bytes,
                                  msg_size - total_read_bytes, 0);
            if (read_bytes > 0)
                total_read_bytes += read_bytes;
            else if (read_bytes < 0)
            {
                LOG(FATAL) << "read socket failed: " << strerror(errno);
            }
            else
                rc = read_bytes;
        }
        return rc;
    }

    int TCPConnector::recv_data_force(int msg_size, char *data_placeholder)
    {
        int sock = get_or_build_socket();
        int total_read_bytes = 0;

        while (total_read_bytes < msg_size)
        {
            // int read_bytes = read(sock, data_placeholder + total_read_bytes,
            //                       msg_size - total_read_bytes);

            int read_bytes = recv(sock, data_placeholder + total_read_bytes,
                                  msg_size - total_read_bytes, 0);
            if (read_bytes > 0)
                total_read_bytes += read_bytes;
            else if (read_bytes < 0)
            {
                LOG(FATAL) << "read socket failed: " << strerror(errno);
            }
            else
            {
                LOG_EVERY_N(WARNING, 100000) << "Cannot receive any data from socket";
                //continue;
            }
        }
        return total_read_bytes;
    }
    int TCPConnector::allocate_socket(std::string info)
    {
        int tmp_socket = 0;
        VLOG(3) << "Preparing socket for " << info;

        tmp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tmp_socket <= 0)
            LOG(FATAL) << "Error of creating socket for " << info;
        LOG(INFO) << "Creating a socket: " << tmp_socket << " for " << info;

        newplan::IPQoSHelper::set_and_check_ip_tos(tmp_socket, 0x10);
        newplan::IPQoSHelper::set_and_check_ip_priority(tmp_socket, 4);
        newplan::IPQoSHelper::set_socket_reuse(tmp_socket);
        VLOG(3) << "[Done] Preparing socket...";

        return tmp_socket;
    }

}; // end namespace rdma_core