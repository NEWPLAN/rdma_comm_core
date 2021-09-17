
#include "util/logging.h"
#include <errno.h>

#include "rdma_channel.h"
#include "rdma_device.h"
#include <unistd.h>
extern int errno;
namespace rdma_core
{
    static int get_cache_line_size()
    {
        int size = 0;
#if !defined(__FreeBSD__)
        size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if (size == 0)
        {
#if defined(__sparc__) && defined(__arch64__)
            char *file_name =
                (char *)"/sys/devices/system/cpu/cpu0/l2_cache_line_size";
#else
            char *file_name =
                (char *)"/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size";
#endif

            FILE *fp;
            char line[10];
            fp = fopen(file_name, "r");
            if (fp == NULL)
            {
                return DEF_CACHE_LINE_SIZE;
            }
            if (fgets(line, 10, fp) != NULL)
            {
                size = atoi(line);
                fclose(fp);
            }
        }
#endif
        if (size <= 0)
            size = DEF_CACHE_LINE_SIZE;

        return size;
    }

    int RDMADevice::cache_line_size = get_cache_line_size();
    int RDMADevice::cycle_buffer = sysconf(_SC_PAGESIZE);

    std::mutex RDMADevice::device_global_lock_; // global lock for use
    std::unordered_map<std::string,
                       std::shared_ptr<RDMADevice>>
        RDMADevice::device_map_; // device map

    RDMADevice::~RDMADevice()
    {
        TRACE_IN;
        if (this->ib_ctx_)
            ibv_close_device(this->ib_ctx_);
        // UNIMPLEMENTED;
        VLOG(0) << "RDMADevice(" << dev_name_ << ") is released";
        TRACE_OUT;
    }

    RDMADevice::RDMADevice(std::string &dev_name) :
        dev_name_(dev_name)
    {
        VLOG(2) << "Creating RDMADevice(" << dev_name_ << ")";
        init_resources();
    }

    bool RDMADevice::register_adapter(RDMAAdapter *adapter)
    {
        TRACE_IN;
        VLOG(3) << "Try to register a RDMAAdapter in " << this->info();
        if (adapter == nullptr)
        {
            LOG(WARNING) << "Adapter is null, register failed";
            TRACE_OUT;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(this->device_local_lock_);
            if (adapter_set_.find(adapter) == adapter_set_.end())
            {
                adapter_set_.insert(adapter);
                VLOG(2) << "[Success]: Register " << adapter->info() << " into " << this->info();
                TRACE_OUT;
                return true;
            }
        }
        LOG(WARNING) << "[Failed]: " << adapter->info() << " is already in " << this->info();
        TRACE_OUT;
        return false;
    }

    std::shared_ptr<RDMADevice> RDMADevice::get_device(std::string dev_name, //device name of this device
                                                       RDMAAdapter *adapter) //device are queried by dev_name
    {
        TRACE_IN;
        std::shared_ptr<RDMADevice> dev_handler = nullptr;
        CHECK(adapter != nullptr) << "[ERROR]: Adapter cannot be empty";
        VLOG(3) << "Try to get the RDMADevice(" << dev_name << ") for " << adapter->info();
        {
            std::lock_guard<std::mutex> lock(RDMADevice::device_global_lock_);
            auto iter = RDMADevice::device_map_.find(dev_name);
            if (iter == RDMADevice::device_map_.end())
            {
                VLOG(3) << "Warning: RDMADevice (" << dev_name << ") do not exist";
                dev_handler.reset(new RDMADevice(dev_name));
                RDMADevice::device_map_.insert({dev_name, dev_handler});
            }
            else
            {
                dev_handler = iter->second;
                VLOG(2) << "Warning: " << dev_handler->info() << " has already been created";
            }
        }
        dev_handler->register_adapter(adapter);
        VLOG(3) << "return RDMADevice(" << dev_name << ") for " << adapter->info();
        TRACE_OUT;
        return dev_handler;
    }

    void RDMADevice::load_sys_params()
    {
        RDMADevice::cache_line_size = get_cache_line_size();
        RDMADevice::cycle_buffer = sysconf(_SC_PAGESIZE);
        VLOG(3) << RLOG::make_string("The sys params are: cache_line: %d, page_size: %d\n", RDMADevice::cache_line_size, RDMADevice::cycle_buffer);
    }

    bool RDMADevice::init_resources()
    {
        TRACE_IN;
        load_sys_params();
        open_device();
        VLOG(2) << "Open device (" << dev_name_ << "@"
                << this->ib_ctx_ << ")";
        query_hardware_info();
        TRACE_OUT;
        return false;
    }

    void RDMADevice::query_hardware_info()
    {
        TRACE_IN;
        // Query device attributes
        if (ibv_query_device(this->ib_ctx_, &(this->device_attr_)) != 0)
            LOG(FATAL) << "Fail to query device attributes in RDMADevice(" << dev_name_ << ")";

        VLOG(3) << RLOG::make_string("RDMADevice(%s) has %u physical ports",
                                     dev_name_.c_str(), device_attr_.phys_port_cnt);

        for (uint8_t port_index = 1; port_index <= device_attr_.phys_port_cnt; port_index++)
            query_port_attr(port_index);
        TRACE_OUT;
        return;
    }

    void RDMADevice::query_port_attr(uint8_t ib_port)
    {
        TRACE_IN;
        struct ibv_port_attr port_attr;
        /* query port properties */
        if (ibv_query_port(this->ib_ctx_, ib_port, &port_attr))
        {
            LOG(FATAL) << RLOG::make_string("failed to Query (%u)th port of RDMADevice(%s)",
                                            ib_port, dev_name_.c_str());
        }

        this->ports_attrs_.push_back(port_attr);

        CHECK(port_attr.state == IBV_PORT_ACTIVE) << "Error, detected the port (" << ib_port << ")"
                                                  << dev_name_ << " is not working properly";

        std::string ref = "https://github.com/linux-rdma/rdma-core/blob/"
                          "486ecb3f12ab17e4b7970a6d5444cd165cec6ee4/libibverbs/verbs.h#L423";
        VLOG(3) << RLOG::make_string("Info on (%u)th port of RDMADevice(%s) is:\n"
                                     "port_state: %d, max_mtu: %d, activate_mtu: %d, "
                                     "lid: %x, sm_lid: %x, link_layer=%d.\nFor more details, please ref to[%s]",
                                     ib_port, dev_name_.c_str(), port_attr.state,
                                     port_attr.max_mtu, port_attr.active_mtu, port_attr.lid,
                                     port_attr.sm_lid, port_attr.link_layer, ref.c_str());
        TRACE_OUT;
        return;
    }

    bool RDMADevice::open_device()
    {
        TRACE_IN;
        struct ibv_device **dev_list = NULL;
        struct ibv_device *ib_dev = NULL;

        int i;
        int num_devices;

        /* get device names in the system */
        dev_list = ibv_get_device_list(&num_devices);
        if (!dev_list)
            LOG(FATAL) << "failed to get IB devices list";

        /* if there isn't any IB device in host */
        if (!num_devices)
            LOG(FATAL) << "found " << num_devices << " device(s)";

        VLOG(2) << "found " << num_devices << " device(s)";

        /* search for the specific device we want to work with */
        for (i = 0; i < num_devices; i++)
        {
            if (dev_name_.length() == 0)
            {
                dev_name_ = strdup(ibv_get_device_name(dev_list[i]));
                VLOG(2) << "device not specified, using first one found: " << dev_name_;
            }
            if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name_.c_str()))
            {
                ib_dev = dev_list[i];
                break;
            }
        }

        /* if the device wasn't found in host */
        if (!ib_dev)
            LOG(FATAL) << "Not find the IB device: " << dev_name_;

        /* get device handle */
        this->ib_ctx_ = ibv_open_device(ib_dev);
        if (!this->ib_ctx_)
            LOG(FATAL) << "failed to open device " << dev_name_;

        /* We are now done with device list, free it */
        ibv_free_device_list(dev_list);
        TRACE_OUT;
        return true;
    }

    struct ibv_comp_channel *RDMADevice::create_event_channel()
    {
        TRACE_IN;
        struct ibv_comp_channel *event_channel = nullptr;
        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            event_channel = ibv_create_comp_channel(ib_ctx_);
        }
        if (!event_channel)
        {
            LOG(FATAL) << "Failed to create completion channel";
        }
        TRACE_OUT;
        return event_channel;
    }
    struct ibv_pd *RDMADevice::create_protected_domain()
    {
        TRACE_IN;
        struct ibv_pd *pd;
        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            pd = ibv_alloc_pd(ib_ctx_);
        }
        if (!pd)
        {
            LOG(FATAL) << "Failed to create protection domain";
        }
        TRACE_OUT;
        return pd;
    }
    bool RDMADevice::has_created_cq(std::string key)
    {
        std::lock_guard<std::mutex> lock(device_local_lock_);
        return reg_cqs_.find(key) != reg_cqs_.end();
    }

    struct ibv_qp_attr RDMADevice::get_qp_attr(struct ibv_qp *qp,
                                               struct ibv_qp_init_attr *init_attr)
    {
        TRACE_IN;
        VLOG(3) << "Querying the attr of QP (" << qp << ") in " << info();
        CHECK(qp != 0) << "Invalid QP";
        CHECK(init_attr != 0) << "Invalid init qp_attr";
        struct ibv_qp_attr qp_attr;

        memset(&qp_attr, 0, sizeof(qp_attr));

        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            if (ibv_query_qp(qp, &qp_attr, 0, init_attr))
            {
                LOG(WARNING) << "Failed to query qp for " << info() << ", because " << strerror(errno);
            }
        }
        TRACE_OUT;
        return qp_attr;
    }

    struct ibv_cq *RDMADevice::create_completion_queue(std::string cq_key,
                                                       int max_cqe, void *cb_ctx,
                                                       struct ibv_comp_channel *channel,
                                                       int comp_vector)
    {
        TRACE_IN;
        VLOG(3) << "Trying to find the completion queue(CQ), named " << cq_key << " in " << info();
        struct ibv_cq *cq;
        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            auto iter = reg_cqs_.find(cq_key);
            if (iter != reg_cqs_.end())
            {
                VLOG(3) << "CQ, named " << cq_key << " is already registered in RDMADevice("
                        << dev_name_ << ")";
                TRACE_OUT;
                return iter->second;
            }
            else
            {
                cq = ibv_create_cq(ib_ctx_,
                                   max_cqe,
                                   cb_ctx,
                                   channel,
                                   comp_vector);
                reg_cqs_[cq_key] = cq;
                VLOG(3) << "CQ, named " << cq_key << " is not found in RDMADevice(" << dev_name_
                        << "), create one (" << cq << ") now!";
            }
        }
        if (!cq)
        {
            LOG(FATAL) << "Failed to create completion queue, because: " << strerror(errno);
        }
        TRACE_OUT;
        return cq;
    }
    struct ibv_qp *RDMADevice::create_queue_pair(struct ibv_pd *pd,
                                                 struct ibv_qp_init_attr init_attr)
    {
        TRACE_IN;
        struct ibv_qp *qp;
        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            qp = ibv_create_qp(pd, &init_attr);
        }
        if (!qp)
        {
            LOG(FATAL) << "Failed to create completion queue";
        }
        TRACE_OUT;
        return qp;
    }

    union ibv_gid RDMADevice::query_gid(uint8_t ib_port, uint8_t gid_index)
    {
        TRACE_IN;
        CHECK(ib_port >= 1 && ib_port <= ports_attrs_.size()) << "Invalid ib_port";
        CHECK(gid_index >= 0) << "Invalid gid_index";

        union ibv_gid my_gid;
        {
            std::lock_guard<std::mutex> lock(device_local_lock_);
            if (ibv_query_gid(this->ib_ctx_, ib_port, gid_index, &my_gid))
            {
                LOG(FATAL) << RLOG::make_string("could not get gid for port(%d) @ RDMADevice(%s), index %d\n",
                                                ib_port, info().c_str(), gid_index);
            }
        }
        TRACE_OUT;
        return my_gid;
    }

    int RDMADevice::real_poll_completion_queue(struct ibv_cq *cq_,
                                               int expected,
                                               struct ibv_wc *wcs)
    {
        // TRACE_IN;
        CHECK(cq_ != 0) << "The completion queue is empty!";
        CHECK(expected >= 1) << "invalid expected wqe";

        int num_wcqe = ibv_poll_cq(cq_, expected, wcs);
        if (num_wcqe < 0)
        {
            LOG(ERROR) << "ERROR: Encounting an error when poll the completion queue: " << cq_;
        }
        if (num_wcqe != 0)
        {
            VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N + 1) << "polled wqe: " << num_wcqe;
        }
        // TRACE_OUT;
        return num_wcqe;
    }

    bool RDMADevice::real_send_(struct ibv_qp *qp, struct ibv_mr *mr_,
                                void *buffer_addrr, uint32_t msg_size,
                                uint32_t imm_data, uint64_t key)
    {
        TRACE_IN;
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Post send request to SQ ";

        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;

        /* prepare the scatter/gather entry */

        //memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)(buffer_addrr);
        sge.length = msg_size;
        sge.lkey = mr_->lkey;
        /* prepare the send work request */

        // memset(&sr, 0, sizeof(sr));
        sr.next = NULL;
        {
            sr.wr_id = key;
        }
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_SEND_WITH_IMM;
        sr.imm_data = imm_data; // do not considering the byte order

        sr.send_flags = IBV_SEND_SIGNALED;

        /* there is a Receive Request in the responder side, so we won't get any into RNR flow */
        if (ibv_post_send(qp, &sr, &bad_wr))
        {
            RDMAChannel *channel = (RDMAChannel *)key;
            LOG(FATAL) << "[error] failed to post send_request to Channel (" << channel->info()
                       << "), Error: " << strerror(errno) << ", sq: " << channel->get_sqe();
        }
        TRACE_OUT;
        return true;
    }

    bool RDMADevice::real_recv_(struct ibv_qp *qp, struct ibv_mr *mr_,
                                void *buffer_addrr, uint32_t msg_size,
                                uint64_t key)
    {
        TRACE_IN;

        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Post receive request to RQ ";
        struct ibv_recv_wr rr;
        struct ibv_sge sge;
        struct ibv_recv_wr *bad_wr;

        /* prepare the scatter/gather entry */

        //memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)(buffer_addrr);
        sge.length = msg_size;
        sge.lkey = mr_->lkey;
        /* prepare the receive work request */

        //memset(&rr, 0, sizeof(rr));
        rr.next = NULL;
        {
            rr.wr_id = key;
        }
        rr.sg_list = &sge;
        rr.num_sge = 1;

        /* post the Receive Request to the RQ */
        if (ibv_post_recv(qp, &rr, &bad_wr))
        {
            RDMAChannel *channel = (RDMAChannel *)key;
            LOG(FATAL) << "[error] failed to post RR to Channel ("
                       << channel->info()
                       << "), Error: " << strerror(errno)
                       << ", rq: " << channel->get_rqe();
        }
        TRACE_OUT;
        return true;
    }

    bool RDMADevice::real_write_remote_(struct ibv_qp *qp,      // the qp for rdma context
                                        uint32_t rkey,          // the key of remote mem
                                        uint32_t lkey,          // the key of local mem
                                        void *src_addr,         // the address to write
                                        uint64_t dest_addr,     // the dest address to write
                                        uint32_t size_in_bytes, // bytes to write
                                        uint64_t wr_id)         // id of this workrequest

    {
        TRACE_IN;
        struct ibv_sge list;
        list.addr = (uintptr_t)(src_addr);
        list.length = size_in_bytes;
        list.lkey = lkey;

        struct ibv_send_wr wr;
        wr.next = NULL;
        wr.wr_id = wr_id;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_WRITE;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = dest_addr;
        wr.wr.rdma.rkey = rkey;

        //wr.send_flags = 0;
        struct ibv_send_wr *bad_wr;
        int ret_value = ibv_post_send(qp, &wr, &bad_wr);

        if (ret_value)
        {
            RDMAChannel *channel = (RDMAChannel *)wr_id;
            LOG(FATAL) << "[error] failed to post SR to Channel ("
                       << channel->info()
                       << "), Error: " << strerror(ret_value)
                       << ", sq: " << channel->get_sqe();
        }
        TRACE_OUT;
        return true;
    }

    bool RDMADevice::real_write_remote_with_notify_(struct ibv_qp *qp,      // the qp for rdma context
                                                    uint32_t rkey,          // the key of remote mem
                                                    uint32_t lkey,          // the key of local mem
                                                    void *src_addr,         // the address to write
                                                    uint64_t dest_addr,     // the dest address to write
                                                    uint32_t size_in_bytes, // bytes to write
                                                    uint32_t imm_data,      // imm data to notify peer
                                                    uint64_t wr_id)         // id of this workrequest
    {
        TRACE_IN;
        struct ibv_sge list;
        list.addr = (uintptr_t)(src_addr);
        list.length = size_in_bytes;
        list.lkey = lkey;

        struct ibv_send_wr wr;
        wr.next = NULL;
        wr.wr_id = wr_id;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = dest_addr;
        wr.wr.rdma.rkey = rkey;
        wr.imm_data = imm_data;

        struct ibv_send_wr *bad_wr;
        //  wr.send_flags = 0;

        if (ibv_post_send(qp, &wr, &bad_wr))
        {
            RDMAChannel *channel = (RDMAChannel *)wr_id;
            LOG(FATAL) << "[error] failed to post SR to Channel ("
                       << channel->info()
                       << "), Error: " << strerror(errno)
                       << ", sq: " << channel->get_sqe();
        }
        TRACE_OUT;
        return true;
    }

    bool RDMADevice::real_read_remote_(struct ibv_qp *qp,
                                       uint32_t rkey, uint32_t lkey,
                                       void *dest_addr, uint64_t src_addr,
                                       uint32_t size_in_bytes,
                                       uint64_t wr_id)
    {
        TRACE_IN;
        struct ibv_sge list;
        list.addr = (uintptr_t)(dest_addr);
        list.length = size_in_bytes;
        list.lkey = lkey;

        struct ibv_send_wr wr;
        wr.next = NULL;
        wr.wr_id = wr_id;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = src_addr;
        wr.wr.rdma.rkey = rkey;

        struct ibv_send_wr *bad_wr;
        // wr.send_flags = 0;

        if (ibv_post_send(qp, &wr, &bad_wr))
        {
            RDMAChannel *channel = (RDMAChannel *)wr_id;
            LOG(FATAL) << "[error] failed to post SR to Channel ("
                       << channel->info()
                       << "), Error: " << strerror(errno)
                       << ", sq: " << channel->get_sqe();
        }
        TRACE_OUT;
        return true;
    }

    struct ibv_mr *RDMADevice::real_register_mem(struct ibv_pd *pd,
                                                 void *data_ptr, size_t size_in_byte,
                                                 int access_flags, std::string info)
    {
        TRACE_IN;
        CHECK(pd != 0) << "protect domain should not be empty for register mem: " << info;
        if (access_flags == 0)
        {
            LOG(WARNING) << "No flags for the mem buf (" << info
                         << "), using the default access mode: "
                         << "(IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE)";
        }

        access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

        CHECK(size_in_byte > 0) << "Error of register mem buf with size: " << size_in_byte;

        CHECK(data_ptr != 0) << "Error of data_ptr";

        struct ibv_mr *tmp_mr = nullptr;
        {
            std::lock_guard<std::mutex> lock(this->device_local_lock_);
            tmp_mr = ibv_reg_mr(pd, data_ptr, size_in_byte, access_flags);
        }
        if (tmp_mr == NULL || tmp_mr == nullptr)
            LOG(FATAL) << "[error] register mem failed";

        LOG(INFO) << RLOG::make_string("[OK] register mem for (%s):  @%p, with size: %lu, and flags: 0x%x, lkey: 0x%x, rkey: 0x%x",
                                       info.c_str(), data_ptr, size_in_byte, access_flags, tmp_mr->lkey, tmp_mr->rkey);
        TRACE_OUT;

        return tmp_mr;
    }
    bool RDMADevice::real_deregister_mem(struct ibv_mr *mr_, std::string info)
    {
        TRACE_IN;
        int ret = 0;
        {
            std::lock_guard<std::mutex> lock(this->device_local_lock_);
            ret = ibv_dereg_mr(mr_);
        }
        if (ret != 0)
            LOG(FATAL) << "Error of deregister_mem for " << info;
        VLOG(3) << "Successfully deregister mem for " << info;
        TRACE_OUT;
        return true;
    }

}; // end namespace rdma_core