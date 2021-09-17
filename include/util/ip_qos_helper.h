#ifndef __RCL_IP_QOS_HELPER_H__
#define __RCL_IP_QOS_HELPER_H__

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// sudo tc -s qdisc ls dev eth0
// sudo tcpdump -i lo tcp port 1234  -nnvv

#ifdef __cplusplus
namespace newplan
{
    class IPQoSHelper
    {
    public:
#endif
        static int set_and_check_ip_tos(int sock, int tos)
        {
            int old_tos = 0, new_tos = 0;
            int tos_length = sizeof(old_tos);

            if (getsockopt(sock, IPPROTO_IP, IP_TOS, &old_tos, (socklen_t *)&tos_length) < 0)
            {
                printf("[Warning] Get IP tos error\n");
                return -1;
            }

            if (setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0)
            {
                printf("[Warning] set IP tos error\n");
                return -1;
            }

            if (getsockopt(sock, IPPROTO_IP, IP_TOS, &new_tos, (socklen_t *)&tos_length) < 0)
            {
                printf("[Warning] Get IP tos error\n");
                return -1;
            }

            if (new_tos != tos)
            {
                printf("[Warning] set IP ToS error\n");
                return -1;
            }

            printf("[INFO] Having modified IP ToS for socket: %d from 0x%x(old) to 0x%x(new)\n",
                   sock, old_tos, new_tos);
            return 0;
        }

        static int set_and_check_ip_priority(int sock, int prio)
        {
            int old_prio = 0, new_prio = 0;
            int prio_length = sizeof(old_prio);

            if (prio > 6 || prio < 0)
            {
                printf("[Warning] IP priority: %d should be in [0,6]\n", prio);
                return -1;
            }

            if (getsockopt(sock, SOL_SOCKET, SO_PRIORITY, &old_prio, (socklen_t *)&prio_length) < 0)
            {
                printf("[Warning] Get IP priority error\n");
                return -1;
            }

            if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)) < 0)
            {
                printf("[Warning] set IP priority error\n");
                return -1;
            }

            if (getsockopt(sock, SOL_SOCKET, SO_PRIORITY, &new_prio, (socklen_t *)&prio_length) < 0)
            {
                printf("[Warning] Get IP priority error\n");
                return -1;
            }

            if (new_prio != prio)
            {
                printf("[Warning] set IP priority error\n");
                return -1;
            }

            printf("[INFO] Having modified IP priority for socket: %d from %d(old) to %d(new)\n",
                   sock, old_prio, new_prio);
            return 0;
        }

        static int set_socket_reuse(int sock)
        {
            int opt = 1;
            // sockfd为需要端口复用的套接字
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                           (const void *)&opt, sizeof(opt))
                < 0)
            {
                printf("[Warning] failed to set IP port resue\n");
                return -1;
            }
            printf("[INFO] Having set IP port resue\n");
            return 0;
        }
#ifdef __cplusplus
    };
};     // namespace newplan
#endif //cplusplus

#endif /* __NEWPLAN_IP_QOS_H*/