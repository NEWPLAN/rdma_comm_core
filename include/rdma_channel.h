/*********************************************************************
 *  RDMAChannel is the high-level abstract of RDMA communication operation
 *  It inherbits the RDMAAdapter to expose operations in user-space
 *  https://www.zhihu.com/column/c_1231181516811390976
 * *******************************************************************/

#ifndef __RDMA_COMM_CORE_RDMA_CHANNEL_H__
#define __RDMA_COMM_CORE_RDMA_CHANNEL_H__

#include "config.h"
#include "rdma_adapter.h"
#include "rdma_buffer.h"
#include "rdma_endpoint.h"
#include <unordered_map>

namespace rdma_core
{
    class RDMABuffer;
    class RDMAEndPoint;

    std::string MsgToStr(MessageType type_);

    class RDMAChannel : public RDMAAdapter
    {
    protected:
        explicit RDMAChannel(Config &con,
                             std::string id,
                             RDMAEndPoint *owned_ep);

    public:
        virtual ~RDMAChannel();
        inline static RDMAChannel *build_rdma_channel(Config &con,
                                                      std::string id,
                                                      RDMAEndPoint *owned_ep)
        {
            return new RDMAChannel(con, id, owned_ep);
        }

    public:
        virtual bool register_buffer(RDMABuffer *buffer);
        virtual bool remove_buffer(RDMABuffer *buffer);

        RDMABuffer *find_buffer(std::string key);
        inline void setup_index_in_session(int32_t index)
        {
            this->indexed_by_session = index;
        }
        inline int get_index_in_session()
        {
            return this->indexed_by_session;
        }

        // show the infomation of this adapter
        inline std::string info()
        {
            return "RDMAChannel@" + get_id();
        }

        virtual std::string get_id();

        bool insert_peer_buffer(std::string key,               // the key
                                struct CommDescriptor &peer_); // the peer info
        struct CommDescriptor *find_peer_buffer(std::string key);

        inline RDMAEndPoint *get_registered_endpoint()
        {
            return registered_endpoint_;
        }

    private:
        Config work_env_;                         // capture the work envirionment
        std::unordered_map<std::string,           // key
                           struct CommDescriptor> // peer buffer
            peer_buffer_mgr_;
        RDMAEndPoint *registered_endpoint_ = nullptr; // the owned endpoint
    };
}; // namespace rdma_core
#endif