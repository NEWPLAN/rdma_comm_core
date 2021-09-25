#include "rdma_adapter.h"
#include "rdma_device.h"
#include "util/logging.h"
#include <errno.h>
extern int errno;

namespace rdma_core
{
    RDMAAdapter::~RDMAAdapter()
    {
        TRACE_IN;
        VLOG(3) << "RDMAAdapter is released";
        TRACE_OUT;
    }

    RDMAAdapter::RDMAAdapter(std::string info_) :
        id_(info_)
    {
        TRACE_IN;
        VLOG(3) << "Creating a (virtual) adapter for " << info_;
        vadapt_status_ = AdapterState::RESET;
        TRACE_OUT;
    }

    AdapterInfo RDMAAdapter::loading()
    {
        TRACE_IN;
        if (resource_is_allocated)
        {
            LOG(WARNING) << "resources for " << info()
                         << " are already allocated";
            TRACE_OUT;
            return self_;
        }

        if (_adapter_config_.using_shared_cq)
        {
            VLOG(3) << info() << " would use shared_cq: " << _adapter_config_.cq_key;
            CHECK(_adapter_config_.cq_key.length() != 0) << "Configure ERROR: " << info()
                                                         << " is expected to use shared cq, but the cq_key is not set";
        }
        else
        {
            _adapter_config_.cq_key = info();
            VLOG(3) << info() << " would not use shared_cq, the cq is: " << _adapter_config_.cq_key;
        }
        VLOG(3) << "Preparing the resources of " << this->info();

        rdma_device_ = RDMADevice::get_device(_adapter_config_.dev_name, this);
        create_pd();
        create_compt_channel();
        create_cq();
        create_qp();
        update_my_adapter_info();
        vadapt_status_ = AdapterState::RESOURCE_ALLOCATED;
        resource_is_allocated = true;
        TRACE_OUT;
        return get_adapter_info();
    }

    void RDMAAdapter::create_qp()
    {
        TRACE_IN;
        VLOG(3) << "Trying to create QPair@" << info();
        /* create the Queue Pair */
        memset(&qp_init_attr, 0, sizeof(qp_init_attr));
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.sq_sig_all = 0;

        if (_adapter_config_.signal_all_wqe)
        {
            qp_init_attr.sq_sig_all = 1;
            LOG(INFO) << "CQ@" << info() << " would generate signal for each sqe";
        }
        else
            LOG(WARNING) << "CQ@" << info() << " would not generate a signal for sqe";

        qp_init_attr.send_cq = used_cq_;
        qp_init_attr.recv_cq = used_cq_;
        qp_init_attr.cap.max_send_wr = _adapter_config_.max_send_wr;
        qp_init_attr.cap.max_recv_wr = _adapter_config_.max_recv_wr;
        qp_init_attr.cap.max_send_sge = _adapter_config_.max_send_sge;
        qp_init_attr.cap.max_recv_sge = _adapter_config_.max_recv_sge;
        qp_init_attr.cap.max_inline_data = 0;
        qp_init_attr.srq = NULL;
        CHECK(qp_ == 0) << "queue pair has been instanced";
        qp_ = rdma_device_->create_queue_pair(pd_, qp_init_attr);
        VLOG(3) << "[OK]: Successfully create queue_pair(" << qp_ << ") for " << info();
        TRACE_OUT;
    }
    void RDMAAdapter::create_pd()
    {
        TRACE_IN;
        // created protection domain
        CHECK(pd_ == 0) << "Protected has been instanced";
        pd_ = rdma_device_->create_protected_domain();
        VLOG(3) << "Creating protection domain(" << pd_
                << ") for " << info();
        TRACE_OUT;
    }
    void RDMAAdapter::create_cq()
    {
        TRACE_IN;
        std::string cq_info = "The CQ@" + info();
        CHECK(_adapter_config_.cq_key.size() != 0)
            << "please explicitly speficy the cq_key";

        if (_adapter_config_.using_shared_cq)
        {
            CHECK(_adapter_config_.cq_key.length() != 0) << cq_info
                                                         << " encounts errors, detected the shared_cq is empty";

            used_cq_ = rdma_device_->create_completion_queue(_adapter_config_.cq_key,
                                                             _adapter_config_.cq_size,
                                                             this, event_channel);

            cq_info += RLOG::make_string(" (@%p, named %s) is shared",
                                         used_cq_, _adapter_config_.cq_key.c_str());
        }
        else
        {
            CHECK(!rdma_device_->has_created_cq(_adapter_config_.cq_key))
                << "[ConfigError], you are using privated compt_queue mode, "
                   "detect previous one, indexed by "
                << _adapter_config_.cq_key;
            used_cq_ = rdma_device_->create_completion_queue(_adapter_config_.cq_key,
                                                             _adapter_config_.cq_size,
                                                             this, event_channel);
            cq_info += RLOG::make_string(" (@%p) is privated",
                                         used_cq_, _adapter_config_.cq_key.c_str());
        }
        VLOG(3) << cq_info;
        TRACE_OUT;
        return;
    }

    void RDMAAdapter::show_qp_info(std::string info)
    {
        auto ret = rdma_device_->get_qp_attr(qp_, &qp_init_attr);
        VLOG(3) << RLOG::make_string("Show the qp info in (%s): \nqp_state: %d, mtu: %d",
                                     info.c_str(), ret.qp_state, ret.path_mtu);
    }

    void RDMAAdapter::update_my_adapter_info()
    {
        TRACE_IN;
        VLOG(3) << "Updating the info of " << info();
        CHECK(rdma_device_ != nullptr) << "rdma_device is not initialized";
        self_.gid = rdma_device_->query_gid(_adapter_config_.ib_port, _adapter_config_.gid_index);
        CHECK(qp_ != 0) << "QP in " << info() << " is not initialized yet";
        self_.qp_num = qp_->qp_num;
        self_.link_layer = rdma_device_->get_port_attr(_adapter_config_.ib_port)->link_layer;
        {
            self_.active_mtu = rdma_device_->get_port_attr(_adapter_config_.ib_port)->active_mtu;
            if (static_cast<int32_t>(_adapter_config_.used_mtu) > static_cast<int32_t>(self_.active_mtu))
            {
                LOG(WARNING) << RLOG::make_string("The actived mtu(%d) is less than the config (%d), reset it", static_cast<int32_t>(self_.active_mtu), static_cast<int32_t>(_adapter_config_.used_mtu));
                _adapter_config_.used_mtu = self_.active_mtu;
            }
            else
            {
                self_.active_mtu = _adapter_config_.used_mtu;
            }
        }
        self_.lid = rdma_device_->get_port_attr(_adapter_config_.ib_port)->lid;
        CHECK(info().length() < (sizeof(self_.unique_id) - 20)) << "Invalid adapter unique_id";
        memset(&self_.unique_id, 0, sizeof(self_.unique_id));
        sprintf((char *)self_.unique_id, "%s", info().c_str());
        TRACE_OUT;
        return;
    }

    void RDMAAdapter::create_compt_channel()
    {
        TRACE_IN;
        std::string compt_info = info();
        if (_adapter_config_.use_event_channel)
        {
            event_channel = rdma_device_->create_event_channel();
            compt_info += RLOG::make_string(" works in passive model, "
                                            "creating the event channel (%p)",
                                            event_channel);
        }
        else
        {
            compt_info += RLOG::make_string(" works in active model, i.e., poll it dramatically");
        }

        VLOG(3) << compt_info;
        TRACE_OUT;
    }

    bool RDMAAdapter::connecting(AdapterInfo &peer)
    {
        TRACE_IN;
        CHECK(peer.unique_id[0] != 0) << "Invalid remote adaper";
        update_peer_adapter_info(peer);
        std::string connecting_info = RLOG::make_string("%s is trying to connect to the remote"
                                                        " (i.e., %s)",
                                                        info().c_str(), remote_.unique_id);

        connecting_info += "\n=====================================================================\n";
        connecting_info += RLOG::make_string("=  Local Adapter ID \t= %s\n", self_.unique_id);
        connecting_info += RLOG::make_string("=  Remote Adapter ID \t= %s\n", remote_.unique_id);
        connecting_info += RLOG::make_string("=  Local QP number = 0x%x,\tRemote QP number = 0x%x\n",
                                             self_.qp_num, remote_.qp_num);
        connecting_info += RLOG::make_string("=  Local LID \t= 0x%x,\t\tRemote LID \t= 0x%x\n",
                                             self_.lid, remote_.lid);
        if (_adapter_config_.gid_index >= 0)
        {
            uint8_t *p = (uint8_t *)&self_.gid;
            connecting_info += RLOG::make_string("=  Self GID \t= %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                                                 p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

            p = (uint8_t *)&remote_.gid;
            connecting_info += RLOG::make_string("=  Remote GID \t= %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                                                 p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        }
        connecting_info += "\n=====================================================================\n";

        VLOG(3) << connecting_info;
        modify_qp_to_init();
        show_qp_info("INIT");
        modify_qp_to_rtr();
        show_qp_info("RTR");
        modify_qp_to_rts();
        show_qp_info("RTS");

        vadapt_status_ = AdapterState::CONNECTING;
        TRACE_OUT;
        return true;
    }

    bool RDMAAdapter::reset_hca()
    {
        //    UNIMPLEMENTED;
        modify_qp_to_reset();

        return false;
    }
    int RDMAAdapter::modify_qp_to_reset()
    {
        // UNIMPLEMENTED;
        VLOG(2) << "reset QP";

        CHECK(this->qp_ != 0) << "QPair are not initialized";
        struct ibv_qp_attr attr;
        int rc;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RESET;

        rc = ibv_modify_qp(this->qp_, &attr, IBV_QP_STATE);
        if (rc)
        {
            LOG(ERROR) << " failed to reset QP state"
                       << ", the reason: " << strerror(errno);
        }
        TRACE_OUT;
        return rc;
    }

    int RDMAAdapter::modify_qp_to_rts()
    {
        TRACE_IN;
        VLOG(2) << "Modify QP to RTS, ready to send";

        CHECK(this->qp_ != 0) << "Qpair has not been initialized";
        struct ibv_qp_attr attr;
        int flags;
        int rc;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = 14;  //0x12;
        attr.retry_cnt = 7; //6;
        attr.rnr_retry = 7; //0;
        attr.sq_psn = 0;
        attr.max_rd_atomic = 1;
        flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
        rc = ibv_modify_qp(this->qp_, &attr, flags);
        if (rc)
            LOG(ERROR) << "failed to modify QP state to RTS, the reason: " << strerror(errno);
        TRACE_OUT;
        return rc;
    }

    int RDMAAdapter::modify_qp_to_rtr()
    {
        TRACE_IN;
        VLOG(2) << "Modify QP to RTR, ready to receive";

        CHECK(this->qp_ != 0) << "Qpair has not been initialized";
        struct ibv_qp_attr attr;
        int flags;
        int rc;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = _adapter_config_.used_mtu;
        {
            VLOG(3) << "Configuure the path_mtu to: "
                    << static_cast<int32_t>(_adapter_config_.used_mtu);
        }
        attr.dest_qp_num = remote_.qp_num;
        attr.rq_psn = 0;
        attr.max_dest_rd_atomic = 1;
        attr.min_rnr_timer = 12; //0x12;
        attr.ah_attr.is_global = 0;
        attr.ah_attr.dlid = remote_.lid;
        attr.ah_attr.sl = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.ah_attr.port_num = _adapter_config_.ib_port;

        if (self_.link_layer == IBV_LINK_LAYER_ETHERNET)
        {
            CHECK(self_.link_layer == remote_.link_layer)
                << "QP should working in the same mode";
            VLOG(3) << "Using RoCE";
            CHECK(_adapter_config_.gid_index >= 0)
                << "Running under RoCE requires to speficy the gid_index";
            attr.ah_attr.is_global = 1;
            attr.ah_attr.port_num = 1;
            attr.ah_attr.grh.dgid = remote_.gid;
            attr.ah_attr.grh.flow_label = 0;
            attr.ah_attr.grh.hop_limit = 0xff;
            attr.ah_attr.grh.sgid_index = _adapter_config_.gid_index;
            attr.ah_attr.grh.traffic_class = _adapter_config_.traffic_class;
            LOG(WARNING) << "Using traffic class "
                         << _adapter_config_.traffic_class / 32;
        }
        flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
        rc = ibv_modify_qp(this->qp_, &attr, flags);

        if (rc)
            LOG(ERROR) << " failed to modify QP state to RTR, the reason: " << strerror(errno);
        TRACE_OUT;
        return rc;
    }
    int RDMAAdapter::modify_qp_to_init()
    {
        TRACE_IN;
        VLOG(2) << "Modifying QP to INIT, preparing to work";

        CHECK(this->qp_ != 0) << "QPair are not initialized";
        struct ibv_qp_attr attr;
        int flags;
        int rc;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_INIT;
        attr.port_num = _adapter_config_.ib_port;
        attr.pkey_index = 0;
        attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
        flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

        rc = ibv_modify_qp(this->qp_, &attr, flags);
        if (rc)
            LOG(ERROR) << " failed to modify QP state to INIT, the reason: " << strerror(errno);
        TRACE_OUT;
        return rc;
    }

    AdapterInfo RDMAAdapter::get_adapter_info(bool self)
    {
        TRACING("");
        AdapterInfo *adapt_info = 0;
        if (self == true)
        {
            adapt_info = &self_;
        }
        else
        {
            adapt_info = &remote_;
        }
        return *adapt_info;
    }

    void RDMAAdapter::update_peer_adapter_info(AdapterInfo &params) // set peer runtime params
    {
        TRACING("");
        remote_ = params;
        CHECK(params.link_layer = self_.link_layer);
    }

    struct ibv_mr *RDMAAdapter::register_mem(void *data_ptr,      //data ptr
                                             size_t size_in_byte, // buffer size
                                             int access_flags,    // access mode read/write
                                             std::string info)    //register the mm buffer info
    {
        return rdma_device_->real_register_mem(this->pd_,
                                               data_ptr,
                                               size_in_byte,
                                               access_flags,
                                               info);
    }

    //deregister mem for the region!
    bool RDMAAdapter::deregister_mem(struct ibv_mr *mr_, // mr_ is the memory key
                                     std::string info)   //register the mm buffer info

    {
        return rdma_device_->real_deregister_mem(mr_, info);
    }

    bool RDMAAdapter::send_remote(RDMABuffer *buffer,            // placeholder for data to send
                                  uint32_t data_length_in_bytes, // data length to send
                                  uint32_t msg_tag)              // signal to notify peer
    {
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";
        buffer->security_check();
        CHECK(buffer->buffer_size >= data_length_in_bytes) << "Invalid data length to send: "
                                                           << data_length_in_bytes
                                                           << ", exceeding the maximum buffer size: "
                                                           << buffer->buffer_size;
        this->increase_sqe();
        return RDMADevice::real_send_(this->qp_, buffer->mr_,
                                      buffer->data_ptr,
                                      data_length_in_bytes,
                                      msg_tag,
                                      (uint64_t)this);
    }

    bool RDMAAdapter::recv_remote(RDMABuffer *buffer,            // placeholder for data to recv
                                  uint32_t data_length_in_bytes) // data length to recv

    {
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";
        buffer->security_check();

        CHECK(buffer->buffer_size >= data_length_in_bytes) << "Invalid data length to recv: "
                                                           << data_length_in_bytes
                                                           << ", exceeding the maximum buffer size: "
                                                           << buffer->buffer_size;
        this->increase_rqe();
        return RDMADevice::real_recv_(this->qp_, buffer->mr_,
                                      buffer->data_ptr,
                                      data_length_in_bytes,
                                      (uint64_t)this);
    }

    // using the adapter to read data from its peer adapter
    bool RDMAAdapter::read_remote(RDMABuffer *buffer,                              // placeholder that cache the read data
                                  uint32_t data_length_in_bytes,                   // data length to read
                                  struct CommDescriptor *remote_buffer_descriptor) // remote buffer_describptor
    {
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";
        buffer->security_check();

        CHECK((data_length_in_bytes <= buffer->buffer_size && data_length_in_bytes <= remote_buffer_descriptor->buffer_length_))
            << "Invalid data length to read";
        this->increase_sqe();
        return RDMADevice::real_read_remote_(this->qp_,
                                             remote_buffer_descriptor->rkey_,
                                             buffer->mr_->lkey,
                                             buffer->data_ptr,
                                             remote_buffer_descriptor->buffer_addr_,
                                             data_length_in_bytes,
                                             (uint64_t)this);
        return true;
    }

    // using the adapter to write the data to its peer adapter
    bool RDMAAdapter::write_remote(RDMABuffer *buffer,                              // the buffer for data to write
                                   uint32_t data_length_in_bytes,                   // data length to read
                                   struct CommDescriptor *remote_buffer_descriptor, // remote buffer_describptor
                                   uint32_t msg_tag,                                // tagging the message type to notify peer
                                   bool notify_peer)                                // notify peer explicit or not
    {
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";
        buffer->security_check();

        CHECK((data_length_in_bytes <= buffer->buffer_size && data_length_in_bytes <= remote_buffer_descriptor->buffer_length_))
            << "Invalid data length to write: " << data_length_in_bytes << ", my buffer size: "
            << buffer->buffer_size << ", peer buffer size: " << remote_buffer_descriptor->buffer_length_;
        this->increase_sqe();

        if (notify_peer)
        {
            return RDMADevice::real_write_remote_with_notify_(this->qp_,
                                                              remote_buffer_descriptor->rkey_,
                                                              buffer->mr_->lkey,
                                                              buffer->data_ptr,
                                                              remote_buffer_descriptor->buffer_addr_,
                                                              data_length_in_bytes,
                                                              msg_tag,
                                                              (uint64_t)this);
        }
        else
        {
            return RDMADevice::real_write_remote_(this->qp_,
                                                  remote_buffer_descriptor->rkey_,
                                                  buffer->mr_->lkey,
                                                  buffer->data_ptr,
                                                  remote_buffer_descriptor->buffer_addr_,
                                                  data_length_in_bytes,
                                                  (uint64_t)this);
        }
        return true;
    }

    // poll a batch of completion events from this adapter
    int RDMAAdapter::poll_cq_batch(struct ibv_wc *wc, // placeholder to recv polled cqe
                                   int num_wqe)       // how many cqes expected to poll
    {
        return RDMADevice::real_poll_completion_queue(this->used_cq_,
                                                      num_wqe,
                                                      wc);
        return 0;
    }

}; // end namespace rdma_core