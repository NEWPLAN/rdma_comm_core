/******************************************************************
 * RDMAAdapter is the low-level of RDMA communication operators
 * It is the base class of RDMAChannel(exposed to user-space)
 * It is the manager of a RDMA connection, which contains
 *      -- qp, a handler of rdma communication in verbs
 *      -- pd, protection domain for operations on qp
 *      -- cq, may be shared with other connections 
 * ****************************************************************/

#ifndef __RDMA_COMM_CORE_RDMAADAPTER_H__
#define __RDMA_COMM_CORE_RDMAADAPTER_H__
#include "rdma_device.h"

#include "rdma_adapter.h"
#include "rdma_buffer.h"
#include "util/logging.h"
#include <infiniband/verbs.h>

#include <map>
#include <memory>
#include <unordered_map>
namespace rdma_core
{
    class RDMADevice;
    class RDMABuffer;

    enum class MessageType : uint32_t
    {
        UNSET = 0,
        RAW_DATA_BLOCK = 1,
        TEST_FOR_CONNECTION = 2,
        TEST_FOR_SYNC_DATA = 3,
        SAY_HELLO = 127,
        REQUEST_EXCHANGE_KEY = (1u << 31) + 1,
        RESPONSE_EXCHANGE_KEY = (1u << 31) + 2
    };

    struct CommDescriptor
    {
        uint64_t buffer_addr_; // buffer_addr registered in RDMA
        size_t buffer_length_; // buffer length
        uint32_t rkey_;        // rkey for peer identify the data
        uint32_t fd_;          // socket fd, only used in TCP/IP model
    } __attribute__((packed));

    enum class AdapterState : uint32_t
    {
        UNUSED = 0,
        RESET = 1,
        RESOURCE_ALLOCATED = 2,
        CONNECTING = 3
    };

    struct AdapterConfig
    {
        std::string unique_id = "Adapter";    // unique id of this adapter
        std::string dev_name = "mlx5_0";      // the device name of using
        uint32_t traffic_class = 0;           // which class the adapter using
        uint32_t cq_size = 1024;              // cq_size of the completion
        int ib_port = 1;                      // the ib_port of this adapter using
        int gid_index = 3;                    // gid index to use
        enum ibv_mtu used_mtu = IBV_MTU_4096; // used mtu of the path
        std::string cq_key = "";              // the completion_queue id, same id means shared_cq
        bool signal_all_wqe = false;          //signal all events
        bool using_shared_cq = true;          // whether should use shared completion
        bool use_event_channel = false;       // whether use event channel
        uint32_t max_send_wr = 128;           //maximum send_wqe in parallel
        uint32_t max_recv_wr = 128;           //maximum recv_wqe in parallel
        uint32_t max_send_sge = 1;            //maximum send_sge in parallel
        uint32_t max_recv_sge = 1;            //maximum recv_sge in parallel
        uint32_t max_inline_data = 1;         //maximum inline_sge in parallel
    };

    struct AdapterInfo
    {
        union ibv_gid gid;                     // gid info
        uint32_t qp_num = 0;                   // QP number
        uint16_t lid = 0;                      // LID of the IB port
        uint8_t link_layer = 0;                // used link layer
        enum ibv_mtu active_mtu = IBV_MTU_256; // active mtu;
        uint8_t unique_id[128] = {0};          // id of this adapter_info
    } __attribute__((packed));

    class RDMAAdapter
    {
    public:
        virtual ~RDMAAdapter();
        virtual bool register_buffer(RDMABuffer *buffer) = 0;
        virtual bool remove_buffer(RDMABuffer *buffer) = 0;

    public:
        inline std::string info() // show the infomation of this adapter
        {
            return "RDMAAdapter@" + get_id();
        }

        virtual std::string get_id() = 0;

        void show_qp_info(std::string info = ""); //show the detail of qp_info

        // building the adapter, i.e., allocating resources it needs
        AdapterInfo loading();

        // connecting to peer adapter
        bool connecting(AdapterInfo &peer);
        bool reset_hca();

        // check whether it will use shared completion queue[for config verification]
        inline bool using_share_completQ()
        {
            return _adapter_config_.using_shared_cq;
        }

        // get the completion queue this adapter is using
        inline struct ibv_cq *get_completQ()
        {
            return this->used_cq_;
        }

        // get the configuration it is using
        inline struct AdapterConfig &get_config()
        {
            return _adapter_config_;
        }

        // get the adapter (runtime params)info in details.
        // By default, return self side
        AdapterInfo get_adapter_info(bool self = true);

        // set peer runtime params
        void update_peer_adapter_info(AdapterInfo &params);

        // register memory for use
        struct ibv_mr *register_mem(void *data_ptr,         //data ptr
                                    size_t size_in_byte,    // buffer size
                                    int access_flags = 0,   // access mode read/write
                                    std::string info = ""); //register the mm buffer info

        //deregister the memory region from this adapter!
        bool deregister_mem(struct ibv_mr *mr_,     // mr_ is the memory key
                            std::string info = ""); //register the mm buffer info

        inline void increase_sqe()
        {
            sq_inflight++;
        }
        inline void increase_rqe()
        {
            rq_inflight++;
        }
        inline void decrease_sqe()
        {
            sq_inflight--;
        }
        inline void decrease_rqe()
        {
            rq_inflight--;
        }
        inline int64_t get_sqe()
        {
            return sq_inflight;
        }
        inline int64_t get_rqe()
        {
            return rq_inflight;
        }

    protected:
        // by default, do not expost the construction function to the outside
        explicit RDMAAdapter(std::string info_);

    private:
        // qpair machine state: https://insujang.github.io/2020-02-09/introduction-to-programming-infiniband/
        int modify_qp_to_reset();      //modify the queuePair to state: reset
        int modify_qp_to_rts();        //modify the queuePair to state: ready to send
        int modify_qp_to_rtr();        //modify the queuePair to state: ready to recv
        int modify_qp_to_init();       //modify the queuePair to state: ready to init
        void create_qp();              //create qpair;
        void create_pd();              //create pd;
        void create_cq();              //create cq;
        void create_compt_channel();   //create completion channel, if we use event
        void update_my_adapter_info(); //update this adapt info

    protected:
        std::string id_ = "DefaultRDMAAdapter";             // the id of this adapter;
        AdapterState vadapt_status_ = AdapterState::UNUSED; // whether the channel is ready
        AdapterConfig _adapter_config_;                     //the runtime time params
        AdapterInfo self_, remote_;                         // the runtime info of (local, and peer) adapter
        bool resource_is_allocated = false;                 //indicating resource are created
        std::shared_ptr<RDMADevice> rdma_device_ = nullptr; // which RDMADevice the virtual adapter is using
        std::map<std::string, RDMABuffer *> owning_buffers; // the owned buffer that the channel is using
        int64_t sq_inflight = 0;
        int64_t rq_inflight = 0;
        int indexed_by_session = -1;

    private:
        struct ibv_pd *pd_ = 0;                           // PD handle
        struct ibv_qp *qp_ = 0;                           // used_q_pair;
        struct ibv_qp_init_attr qp_init_attr;             // init_qp_status
        struct ibv_cq *used_cq_ = 0;                      // completion queue this adapter used
        struct ibv_comp_channel *event_channel = nullptr; // event_channel if it using the passive mode

    public:
        // using the adapter to send the msg to its peer adapter
        bool send_remote(RDMABuffer *buffer,            // placeholder of the data to send
                         uint32_t data_length_in_bytes, // data length to send
                         uint32_t msg_tag = 0);         // tagging the message type to notify peer

        // using the adapter to recv the msg from its peer adapter
        bool recv_remote(RDMABuffer *buffer,             // a placeholder that captures the recv data
                         uint32_t data_length_in_bytes); // data length to recv

        // using the adapter to read data from its peer adapter
        bool read_remote(RDMABuffer *buffer,                               // placeholder that cache the read data
                         uint32_t data_length_in_bytes,                    // data length to read
                         struct CommDescriptor *remote_buffer_descriptor); // remote buffer_describptor

        // using the adapter to write the data to its peer adapter
        bool write_remote(RDMABuffer *buffer,                              // the buffer for data to write
                          uint32_t data_length_in_bytes,                   // data length to read
                          struct CommDescriptor *remote_buffer_descriptor, // remote buffer_describptor
                          uint32_t msg_tag,                                // tagging the message type to notify peer
                          bool notify_peer = false);                       // notify peer explicit or not

        // poll a batch of completion events from this adapter
        int poll_cq_batch(struct ibv_wc *wc, // placeholder to recv polled cqe
                          int num_wqe);      // how many cqes expected to poll
    };
}; // end of namespace rdma_core
#endif