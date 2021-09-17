/*************************************************************************
 * This is the abstraction of RDMA device, indexed by device name
 * It takes control of all operations that related to hardware.
 * There are two types of operations:
 *   -- get the physical resources related to the NIC., e.g., 
 *          allocate cq/pd/compt_channel, etc. , protedted by global lock
 *   -- per pd related workflows, e.g.,
 *          submitting tasks (send/recv/write/read)
 * ***********************************************************************/

#ifndef __RDMA_COMM_CORE_RDMA_DEVICE_H__
#define __RDMA_COMM_CORE_RDMA_DEVICE_H__

#include "util/logging.h"

#include <infiniband/verbs.h>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#define DEF_CACHE_LINE_SIZE (64)
#define DEF_PAGE_SIZE (4096)
#include "rdma_adapter.h"
namespace rdma_core
{
    class RDMAAdapter;

    class RDMADevice
    {
    public:
        virtual ~RDMADevice();

    public:
        /***************public API for function call******************/

        static std::shared_ptr<RDMADevice> get_device( //static method to get a device instance
            std::string dev_name,                      //device name of this device
            RDMAAdapter *adapter);                     //device are queried by dev_name

        struct ibv_comp_channel *create_event_channel(); // Completion channel

        struct ibv_pd *create_protected_domain(); // Protection domain

        struct ibv_cq *create_completion_queue(
            std::string cq_key,               // the cq_key;
            int max_cqe,                      //how many the complequeue can holds
            void *cb_ctx,                     // call_backed context registered in qp
            struct ibv_comp_channel *channel, // event channel
            int comp_vector = 0);             // CQ will use comp_vector for signaling completion events;

        struct ibv_qp *create_queue_pair(
            struct ibv_pd *pd,                  // the protection domain
            struct ibv_qp_init_attr init_attr); // create queue pair for real RDMA connection

        inline struct ibv_device_attr *get_device_attr() // get the devide attributes
        {
            return &device_attr_;
        }

        inline struct ibv_port_attr *get_port_attr( // get the port attribute
            uint ib_port)                           // the port to query
        {
            CHECK(ib_port >= 1 && ib_port <= device_attr_.phys_port_cnt)
                << "Invalid port index";
            return &ports_attrs_[ib_port - 1];
        }

        struct ibv_qp_attr get_qp_attr(          // query the qp status
            struct ibv_qp *qp,                   // the qp to queryed
            struct ibv_qp_init_attr *init_attr); // query the qp status

        bool has_created_cq(std::string key); // check the completion queue exists

        union ibv_gid query_gid( // query gid info in this device
            uint8_t ib_port,
            uint8_t gid_index);

        inline std::string info() // return the device info, i.e., dev_name
        {
            return "RDMADevice(" + dev_name_ + ")";
        }

        struct ibv_mr *real_register_mem( // register memory for use
            struct ibv_pd *pd,            // protected domain
            void *data_ptr,               //data ptr
            size_t size_in_byte,          // buffer size
            int access_flags = 0,         // access mode read/write
            std::string info = "");       //register the mm buffer info

        bool real_deregister_mem(   // deregister memory for use
            struct ibv_mr *mr_,     // mr_ is the memory key
            std::string info = ""); //register the mm buffer info

        static int real_poll_completion_queue( // static method to poll completion queue
            struct ibv_cq *cq_,                // poll_from the completion queue
            int expected,                      // expected number of wqe
            struct ibv_wc *wces);              // placeholder to fetch wc

        static bool real_send_( // basic method related to communication
            struct ibv_qp *qp,  // the qp for rdma context
            struct ibv_mr *mr_, // the memory region info
            void *buffer_addrr, // buffer that to recv data
            uint32_t msg_size,  // recv buffer size
            uint32_t imm_data,  // imm_data to notify peer
            uint64_t key);      // key to indicate who send it

        static bool real_recv_(
            struct ibv_qp *qp,  // the qp for rdma context
            struct ibv_mr *mr_, // the memory region info
            void *buffer_addrr, // buffer that to recv data
            uint32_t msg_size,  // recv buffer size
            uint64_t key);      // key to indicate who send it

        static bool real_write_remote_(
            struct ibv_qp *qp,      // the qp for rdma context
            uint32_t rkey,          // the key of remote mem
            uint32_t lkey,          // the key of local mem
            void *src_addr,         // the address to write
            uint64_t dest_addr,     // the dest address to write
            uint32_t size_in_bytes, // bytes to write
            uint64_t wr_id);        // id of this workrequest

        static bool real_write_remote_with_notify_(
            struct ibv_qp *qp,      // the qp for rdma context
            uint32_t rkey,          // the key of remote mem
            uint32_t lkey,          // the key of local mem
            void *src_addr,         // the address to write
            uint64_t dest_addr,     // the dest address to write
            uint32_t size_in_bytes, // bytes to write
            uint32_t imm_data,      // imm data to notify peer
            uint64_t wr_id);        // id of this workrequest

        static bool real_read_remote_(
            struct ibv_qp *qp,      // the qp for rdma context
            uint32_t rkey,          // the key of remote mem
            uint32_t lkey,          // the key of local mem
            void *dest_addr,        // the address to read placeholder
            uint64_t src_addr,      // the  address to read source
            uint32_t size_in_bytes, // bytes to read
            uint64_t wr_id);        // id of this workrequest
        /*******************exposed API************************/

    protected:
        explicit RDMADevice(std::string &dev_name);
        bool register_adapter(RDMAAdapter *adapter);

    private:
        bool init_resources();
        bool open_device();
        void query_hardware_info();
        void query_port_attr(uint8_t ib_port);
        void load_sys_params();

    private:
        // global resources
        static std::mutex device_global_lock_;                 // global lock for use
        static std::unordered_map<std::string,                 // key
                                  std::shared_ptr<RDMADevice>> // value
            device_map_;                                       // device_map

        std::mutex device_local_lock_;                  // local lock for device used
        std::unordered_map<std::string,                 // key
                           struct ibv_cq *>             //value
            reg_cqs_;                                   //completion_queue in this dev
        std::set<RDMAAdapter *> adapter_set_;           //adapter set that using this device
        std::string dev_name_ = "";                     // device name
        struct ibv_context *ib_ctx_ = 0;                /* device handle */
        struct ibv_device_attr device_attr_;            /* Device attributes */
        std::vector<struct ibv_port_attr> ports_attrs_; // IB port attributes

        static int cache_line_size;
        static int cycle_buffer;
    };
}; // namespace rdma_core
#endif