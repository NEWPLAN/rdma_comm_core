#ifndef __NETTOOLS_H__
#define __NETTOOLS_H__

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/types.h>

class NetTool
{
public:
    NetTool()
    {
    }
    virtual ~NetTool()
    {
    }
    static std::string get_ip(const char *ip_prefix, int ip_prefix_length)
    {
        char ip_[60] = {0};
        uint32_t ip = (uint32_t)inet_addr(ip_prefix);
        uint32_t len = ip_prefix_length;
        uint32_t ip_local = (ip << (32 - len)) >> (32 - len);
        uint32_t gtway = ip_local + (1 << ip_prefix_length);
        struct in_addr ip_gateway;
        memcpy(&ip_gateway, &gtway, sizeof(uint32_t));

        sprintf(ip_, "%s", inet_ntoa(ip_gateway));
        ip_[strlen(ip_) - 1] = 0;
        int node_id = get_node_id(ip_prefix, ip_prefix_length);

        std::string this_ip = std::string(ip_prefix, strlen(ip_) - 1);
        this_ip += std::string(".") + std::to_string(node_id);
        //printf("this_ip: %s\n", this_ip.c_str());
        return this_ip;
    }
    static int get_node_id(const char *ip_prefix, int ip_prefix_length)
    {
        char ip_[60] = {0};
        uint32_t ip = (uint32_t)inet_addr(ip_prefix);
        uint32_t len = ip_prefix_length;
        uint32_t ip_local = (ip << (32 - len)) >> (32 - len);
        uint32_t gtway = ip_local + (1 << ip_prefix_length);
        struct in_addr ip_gateway;
        memcpy(&ip_gateway, &gtway, sizeof(uint32_t));

        sprintf(ip_, "%s", inet_ntoa(ip_gateway));
        ip_[strlen(ip_) - 1] = 0;
        ip_prefix_length = strlen(ip_);

        struct ifaddrs *ifAddrStruct = NULL;
        struct ifaddrs *ifa = NULL;
        void *tmpAddrPtr = NULL;

        int node_id = -1;

        getifaddrs(&ifAddrStruct);

        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr)
            {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET) // check it is IP4
            {
                // is a valid IP4 Address
                tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
                char addressBuffer[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
                if (strncmp(addressBuffer, ip_prefix, ip_prefix_length) == 0)
                {
                    node_id = atoi(addressBuffer + ip_prefix_length);
#ifdef DEBUG_SHOW_IP
                    printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
#endif
                }
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6) // check it is IP6
            {
                continue;
                // is a valid IP6 Address
                tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
                char addressBuffer[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
#ifdef DEBUG_SHOW_IP
                printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
#endif
            }
        }
        if (ifAddrStruct != NULL)
        {
            freeifaddrs(ifAddrStruct);
        }

        return node_id;
    }

    static int get_token_id(const char *ip)
    {
        int p1, p2, p3, p4;
        sscanf(ip, "%d.%d.%d.%d", &p1, &p2, &p3, &p4);
        return p4;
    }
};

#endif