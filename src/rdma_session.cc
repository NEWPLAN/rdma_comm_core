#include "rdma_session.h"
#include "rdma_buffer.h"
#include "util/ip_qos_helper.h"
#include <atomic>
#include <thread>

namespace rdma_core
{
    std::string MsgToStr(MessageType type_)
    {
        switch (type_)
        {
            case MessageType::UNSET:
                return "UNSET";
            case MessageType::RAW_DATA_BLOCK:
                return "RAW_DATA_BLOCK";
            case MessageType::TEST_FOR_CONNECTION:
                return "TEST_FOR_CONNECTION";
            case MessageType::TEST_FOR_SYNC_DATA:
                return "TEST_FOR_SYNC_DATA";
            case MessageType::SAY_HELLO:
                return "SAY_HELLO";
            case MessageType::REQUEST_EXCHANGE_KEY:
                return "REQUEST_EXCHANGE_KEY";
            case MessageType::RESPONSE_EXCHANGE_KEY:
                return "RESPONSE_EXCHANGE_KEY";
            default:
                return "Unknown MessageType";
        }
        return "";
    }

    RDMASession::RDMASession(Config &conf) :
        work_env_(conf)
    {
        VLOG(3) << "Allocating basic resources for " << work_env_.role << "@" << work_env_.session_id;
        { // bind function
            established_done_ = std::bind(&RDMASession::default_established_fn, this,
                                          std::placeholders::_1);
            process_send_done_ = std::bind(&RDMASession::default_process_send_done, this,
                                           std::placeholders::_1, std::placeholders::_2);
            process_recv_done_ = std::bind(&RDMASession::default_process_recv_done, this,
                                           std::placeholders::_1, std::placeholders::_2);
            process_recv_write_with_imm_done_ = std::bind(&RDMASession::default_process_recv_write_with_imm_done,
                                                          this, std::placeholders::_1, std::placeholders::_2);
            process_write_done_ = std::bind(&RDMASession::default_process_write_done, this,
                                            std::placeholders::_1, std::placeholders::_2);
            process_read_done_ = std::bind(&RDMASession::default_process_read_done, this,
                                           std::placeholders::_1, std::placeholders::_2);
        }
    }
    RDMASession::~RDMASession()
    {
        // UNIMPLEMENTED;
        VLOG(3) << "Releasing basic resources for " << info();
    }

    void RDMASession::process_CQ(std::vector<RDMAChannel *> aggregated_channels)
    {
        established_done_(aggregated_channels);

        struct ibv_wc global_wc[128];
        do
        {
            for (auto *each_channel : aggregated_channels)
            {
                int num_wqe = each_channel->poll_cq_batch(global_wc, 128);
                //std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (num_wqe == 0)
                    continue;
                VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "The number of polled cqe is " << num_wqe;

                for (int wqe_index = 0; wqe_index < num_wqe; wqe_index++)
                {
                    if (global_wc[wqe_index].status != IBV_WC_SUCCESS)
                    {
                        each_channel->show_qp_info();
                        LOG(FATAL) << "[WARNING]: Encounting unsuccess wqe, for: "
                                   << ibv_wc_status_str(global_wc[wqe_index].status);
                    }

                    switch (global_wc[wqe_index].opcode)
                    {
                        case IBV_WC_SEND:
                        {
                            process_send_done_(&global_wc[wqe_index], 0);
                            break;
                        }
                        case IBV_WC_RECV:
                        {
                            process_recv_done_(&global_wc[wqe_index], 0);
                            break;
                        }
                        case IBV_WC_RDMA_WRITE:
                        {
                            process_write_done_(&global_wc[wqe_index], 0);
                            break;
                        }
                        case IBV_WC_RDMA_READ:
                        {
                            process_read_done_(&global_wc[wqe_index], 0);
                            break;
                        }
                        case IBV_WC_RECV_RDMA_WITH_IMM:
                        {
                            process_recv_write_with_imm_done_(&global_wc[wqe_index],
                                                              0);
                            break;
                        }
                        default:
                        {
                            RDMAChannel *active_channel = (RDMAChannel *)global_wc[wqe_index].wr_id;
                            LOG(FATAL) << "Unknown opcode from " + active_channel->info();
                            break;
                        }
                    }
                }
            }

        } while (true);

        VLOG(3) << "Connection testing is done, everything is OK!";
    }
    void RDMASession::allocate_resources()
    {
        UNIMPLEMENTED;
    }

    void RDMASession::running() //start services
    {
        // std::set<struct ibv_cq *> globalCQ;
        std::unordered_map<struct ibv_cq *, std::vector<RDMAChannel *>> all_channel;
        std::vector<RDMAChannel *> aggregated_channels;
        std::vector<std::thread *> process_threads;
        for (auto &each_endpoint : end_point_mgr_)
        {
            auto *channel = each_endpoint->get_channel();
            if (all_channel.find(channel->get_completQ()) == all_channel.end())
            {
                std::vector<RDMAChannel *> tmp_;
                all_channel.insert({channel->get_completQ(), std::move(tmp_)});
            }
            all_channel[channel->get_completQ()].push_back(channel);

            aggregated_channels.push_back(channel);
        }
        for (auto each : all_channel)
        {
            process_threads.push_back(new std::thread(&RDMASession::process_CQ,
                                                      this,
                                                      each.second));
        }
        for (auto *each_thread : process_threads)
            each_thread->join();
        // process_CQ(aggregated_channels);
    }

    int RDMASession::accept_new_connection(int sock_fd, struct sockaddr_in cin)
    {
        std::string peer_ip = std::string(inet_ntoa(cin.sin_addr));
        int peer_port = htons(cin.sin_port);

        VLOG(2) << info() << " is handling new connect request["
                << peer_ip << ":" << peer_port << "]";
        RDMAEndPoint *new_EndPoint = RDMAEndPoint::create_endpoint(sock_fd, peer_ip,
                                                                   peer_port, work_env_,
                                                                   this);
        //new_EndPoint->connecting();
        end_point_mgr_.push_back(std::move(std::unique_ptr<RDMAEndPoint>(new_EndPoint)));

        new_EndPoint->get_channel()->setup_index_in_session(end_point_mgr_.size() - 1);
        {
            // in_qps.push_back(std::shared_ptr<std::atomic_uint64_t>(new std::atomic_uint64_t(0)));
            // out_qps.push_back(std::shared_ptr<std::atomic_uint64_t>(new std::atomic_uint64_t(0)));
        }
        return 1;
    }

    bool RDMASession::register_event_callback(                                               // register a group of callback functions
        std::function<void(std::vector<RDMAChannel *> g_channels)> established_done,         // when establish is done before start rdma services
        std::function<void(struct ibv_wc *wc, void *args)> process_send_done,                // process when send data done
        std::function<void(struct ibv_wc *wc, void *args)> process_recv_done,                // process when recv data done
        std::function<void(struct ibv_wc *wc, void *args)> process_recv_write_with_imm_done, // process when recv data done
        std::function<void(struct ibv_wc *wc, void *args)> process_write_done,               // process when write data done
        std::function<void(struct ibv_wc *wc, void *args)> process_read_done)                // process when read data done
    {
        VLOG(3) << "Register callback function to process events for " << info();
        if (established_done != nullptr)
            established_done_ = established_done;
        if (process_send_done != nullptr)
            process_send_done_ = process_send_done;
        if (process_recv_done != nullptr)
            process_recv_done_ = process_recv_done;
        if (process_recv_write_with_imm_done != nullptr)
            process_recv_write_with_imm_done_ = process_recv_write_with_imm_done;
        if (process_write_done != nullptr)
            process_write_done_ = process_write_done;
        if (process_read_done != nullptr)
            process_read_done_ = process_read_done;
        return true;
    }

    void RDMASession::default_established_fn(std::vector<RDMAChannel *> g_channels)
    {
        VLOG(3) << "In processing establish events";

        for (auto a_channel : g_channels)
        {
            //RDMAChannel *a_channel = each_point->get_channel();

            // recv_buffer is only used to holds the data send by remote
            RDMABuffer *recv_buffer = RDMABuffer::allocate_buffer(256, 10,
                                                                  "recv_buffer_for_test@" + a_channel->get_id());
            // send_buffer is only used to encode the (local) data sending to remote
            RDMABuffer *send_buffer = RDMABuffer::allocate_buffer(256, 10,
                                                                  "send_buffer_for_test@" + a_channel->get_id());

            // read_buffer is only used to hold the data reading from remote
            RDMABuffer *read_buffer = RDMABuffer::allocate_buffer(1000, 10,
                                                                  "read_buffer_for_test@" + a_channel->get_id());

            // write_buffer is only used to encode the (local) data writing to remote
            RDMABuffer *write_buffer = RDMABuffer::allocate_buffer(1000, 10,
                                                                   "write_buffer_for_test@" + a_channel->get_id());

            // rw_sink_buffer is only used to sink as a placeholder for remote write/read
            RDMABuffer *rw_sink_buffer = RDMABuffer::allocate_buffer(1000, 10,
                                                                     "read_write_sink_buffer_for_test@" + a_channel->get_id());

            a_channel->register_buffer(recv_buffer);
            a_channel->register_buffer(send_buffer);
            a_channel->register_buffer(read_buffer);
            a_channel->register_buffer(write_buffer);
            a_channel->register_buffer(rw_sink_buffer);

            // RDMABuffer *buffer_bin = rw_sink_buffer;
            // for (uint32_t index = 0; index < rw_sink_buffer->get_block_size(); index++)
            // {
            //     buffer_bin->next()->print_buffer_info();
            // }

            recv_buffer->clear();
            a_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);
            a_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);
            //a_channel->recv_remote(rw_sink_buffer, rw_sink_buffer->buffer_size);

            VLOG(3) << "Test register recv_data_buffer to send&recv";
            a_channel->get_registered_endpoint()->sync_with_peer("Everything is ready to start the benchmark");

            std::string base_to_send_data = "Greetings from " + a_channel->info() + " for 'RDMA connection test', count: ";

            std::string tmp_send_buf = base_to_send_data;
            send_buffer->fill_in((uint8_t *)tmp_send_buf.c_str(), tmp_send_buf.length());
            a_channel->send_remote(send_buffer, tmp_send_buf.length(),
                                   static_cast<uint32_t>(MessageType::TEST_FOR_SYNC_DATA));
        } // done
    }
    void RDMASession::default_process_send_done(struct ibv_wc *wc, void *args)
    {
        CHECK(args == 0) << "Unused arguments";
        RDMAChannel *active_channel = (RDMAChannel *)wc->wr_id;
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "get a cqe with opcode: IBV_WC_SEND from " << active_channel->info();
    }

    void RDMASession::default_process_recv_done(struct ibv_wc *wc, void *args)
    {
        CHECK(args == 0) << "Unused arguments";
        RDMAChannel *active_channel = (RDMAChannel *)wc->wr_id;
        RDMABuffer *recv_buffer = active_channel->find_buffer("recv_buffer_for_test@" + active_channel->get_id());
        RDMABuffer *send_buffer = active_channel->find_buffer("send_buffer_for_test@" + active_channel->get_id());
        RDMABuffer *write_buffer = active_channel->find_buffer("write_buffer_for_test@" + active_channel->get_id());

        CHECK((recv_buffer != nullptr) && (send_buffer != nullptr)) << "Invalid buffer query";

        std::string base_to_send_data = "Greetings from " + active_channel->info() + " for 'RDMA connection test'";

        std::string tmp_send_buf = base_to_send_data;

        std::string on_recv_data_info = "get a cqe with opcode: IBV_WC_RECV from " + active_channel->info();

        CHECK(wc->wc_flags & IBV_WC_WITH_IMM) << on_recv_data_info
                                              << ", detected imm data failed to check for IBV_WC_RECV";

        MessageType val = static_cast<MessageType>(wc->imm_data);

        on_recv_data_info += ", imm data: " + std::to_string(wc->imm_data) + ", MSG_TYPE: " + MsgToStr(val) + ", Receive " + std::to_string(wc->byte_len) + " bytes data";

        wc->byte_len = 0;
        if (val == MessageType::REQUEST_EXCHANGE_KEY)
        {
            struct CommDescriptor *peer_buffer = (struct CommDescriptor *)recv_buffer->data_ptr;

            struct CommDescriptor remote_comm_descritor;

            remote_comm_descritor.buffer_addr_ = (uint64_t)peer_buffer->buffer_addr_;
            remote_comm_descritor.buffer_length_ = peer_buffer->buffer_length_;
            remote_comm_descritor.rkey_ = peer_buffer->rkey_;

            active_channel->insert_peer_buffer("rw_sink_buffer_for_test", remote_comm_descritor);

            on_recv_data_info += ", encounting EXCHANGE_KEY_REQUEST: ";
            on_recv_data_info += "\n--------------------------------------------------------\n";
            on_recv_data_info += RLOG::make_string("The remote sinked buffer for read/write test (%s) is: \n"
                                                   "buffer_addr: %p, buffer_size: %u, rkey: 0x%x",
                                                   active_channel->get_id().c_str(),
                                                   (uint8_t *)remote_comm_descritor.buffer_addr_,
                                                   remote_comm_descritor.buffer_length_,
                                                   remote_comm_descritor.rkey_);
            on_recv_data_info += "\n--------------------------------------------------------\n";
            VLOG(3) << on_recv_data_info;
            active_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);
            //recv_buffer->clear();
            active_channel->send_remote(send_buffer, tmp_send_buf.length(),
                                        static_cast<uint32_t>(MessageType::RESPONSE_EXCHANGE_KEY));
            { // launching write
                tmp_send_buf = "[RDMA write/read test]: " + base_to_send_data;
                write_buffer->fill_in((uint8_t *)tmp_send_buf.c_str(), tmp_send_buf.length());
                active_channel->write_remote(write_buffer, tmp_send_buf.length(),
                                             &remote_comm_descritor,
                                             static_cast<uint32_t>(MessageType::RESPONSE_EXCHANGE_KEY),
                                             true); // notify-peer
            }
        }
        else if (val == MessageType::TEST_FOR_SYNC_DATA)
        {
            on_recv_data_info += ", the received data is: " + std::string((char *)recv_buffer->data_addr());
            //recv_buffer->clear();
            active_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);

            struct CommDescriptor local_comm_descritor;
            RDMABuffer *rw_sink_buffer = active_channel->find_buffer("read_write_sink_buffer_for_test@" + active_channel->get_id());

            { //encoding the basic info of rw_sink_buffer for read/write by peer
                local_comm_descritor.buffer_addr_ = (uint64_t)rw_sink_buffer->data_ptr;
                local_comm_descritor.buffer_length_ = rw_sink_buffer->buffer_size;
                local_comm_descritor.rkey_ = rw_sink_buffer->mr_->rkey;

                std::string sink_buffer_info = "\n--------------------------------------------------------\n";
                sink_buffer_info += RLOG::make_string("The local sinked buffer for read/write test (%s) is: \n"
                                                      "buffer_addr: %p, buffer_size: %u, rkey: 0x%x",
                                                      active_channel->get_id().c_str(),
                                                      rw_sink_buffer->data_ptr,
                                                      rw_sink_buffer->buffer_size,
                                                      rw_sink_buffer->mr_->rkey);
                sink_buffer_info += "\n--------------------------------------------------------\n";
                VLOG(3) << sink_buffer_info;
            }

            send_buffer->fill_in((uint8_t *)&local_comm_descritor, sizeof(local_comm_descritor));
            active_channel->send_remote(send_buffer, sizeof(local_comm_descritor),
                                        static_cast<uint32_t>(MessageType::REQUEST_EXCHANGE_KEY));
            // recv_buffer->clear();
        }
        else
        {
            on_recv_data_info += ", the received data is: " + std::string((char *)recv_buffer->data_addr());
            //recv_buffer->clear();
            active_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);
            tmp_send_buf = base_to_send_data;
            send_buffer->fill_in((uint8_t *)tmp_send_buf.c_str(), tmp_send_buf.length());
            active_channel->send_remote(send_buffer, tmp_send_buf.length(),
                                        static_cast<uint32_t>(MessageType::UNSET));
        }
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << on_recv_data_info;
    }
    void RDMASession::default_process_write_done(struct ibv_wc *wc, void *args)
    {
        CHECK(args == 0) << "Unused arguments";
        RDMAChannel *active_channel = (RDMAChannel *)wc->wr_id;
        RDMABuffer *recv_buffer = active_channel->find_buffer("recv_buffer_for_test@" + active_channel->get_id());
        RDMABuffer *read_buffer = active_channel->find_buffer("read_buffer_for_test@" + active_channel->get_id());

        struct CommDescriptor *remote_comm_descritor = active_channel->find_peer_buffer("rw_sink_buffer_for_test");

        CHECK(remote_comm_descritor != nullptr) << "Cannot find the remote buffer info for rw_sink_buffer_for_test";

        CHECK((recv_buffer != nullptr) && (read_buffer != nullptr)) << "Invalid buffer query";

        std::string on_write_data_info = "get a cqe with opcode: IBV_WC_RDMA_WRITE from " + active_channel->info();
        on_write_data_info += ", i.e., Writing " + std::to_string(wc->byte_len) + " bytes to remote. Now, we try to read it back from remote.";
        wc->byte_len = 0;
        { // launching read
            read_buffer->clear();
            active_channel->read_remote(read_buffer,
                                        read_buffer->buffer_size,
                                        remote_comm_descritor);
        }
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << on_write_data_info;
    }
    void RDMASession::default_process_read_done(struct ibv_wc *wc, void *args)
    {
        CHECK(args == 0) << "Unused arguments";
        RDMAChannel *active_channel = (RDMAChannel *)wc->wr_id;
        RDMABuffer *write_buffer = active_channel->find_buffer("write_buffer_for_test@" + active_channel->get_id());
        RDMABuffer *read_buffer = active_channel->find_buffer("read_buffer_for_test@" + active_channel->get_id());
        CHECK((write_buffer != nullptr) && (read_buffer != nullptr)) << "Invalid buffer query";

        struct CommDescriptor *remote_comm_descritor = active_channel->find_peer_buffer("rw_sink_buffer_for_test");

        CHECK(remote_comm_descritor != nullptr) << "Cannot find the remote buffer info for rw_sink_buffer_for_test";

        std::string on_read_data_info = "get a cqe with opcode: IBV_WC_RDMA_READ from " + active_channel->info();
        on_read_data_info += ", i.e., Reading " + std::to_string(wc->byte_len) + " bytes from remote, the data is: " + std::string((char *)read_buffer->data_addr()) + ", Now, we would write it again!";
        wc->byte_len = 0;
        { // launching write
            std::string base_to_send_data = "Greetings from " + active_channel->info() + " for 'RDMA connection test'";
            std::string tmp_send_buf = "[RDMA write/read test]: " + base_to_send_data;
            write_buffer->fill_in((uint8_t *)tmp_send_buf.c_str(), tmp_send_buf.length());
            active_channel->write_remote(write_buffer, tmp_send_buf.length(),
                                         remote_comm_descritor,
                                         static_cast<uint32_t>(MessageType::UNSET),
                                         false); // notify_peer =false;
        }
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << on_read_data_info;
    }

    void RDMASession::default_process_recv_write_with_imm_done(struct ibv_wc *wc, void *args)
    {
        CHECK(args == 0) << "Unused args";
        RDMAChannel *active_channel = (RDMAChannel *)wc->wr_id;
        RDMABuffer *recv_buffer = active_channel->find_buffer("recv_buffer_for_test@" + active_channel->get_id());
        CHECK(recv_buffer != nullptr) << "Invalid buffer query";

        std::string on_recv_data_info = "get a cqe with opcode: IBV_WC_RECV_RDMA_WITH_IMM from " + active_channel->info();

        CHECK(wc->wc_flags & IBV_WC_WITH_IMM) << on_recv_data_info
                                              << ", detected imm data failed to check for IBV_WC_RECV_RDMA_WITH_IMM";

        MessageType val = static_cast<MessageType>(wc->imm_data);

        on_recv_data_info += ", imm data: " + std::to_string(wc->imm_data) + ", MSG_TYPE: " + MsgToStr(val) + ", i.e., peer has written " + std::to_string(wc->byte_len) + " bytes here.";

        wc->byte_len = 0;
        wc->imm_data = 0;

        active_channel->recv_remote(recv_buffer, recv_buffer->buffer_size);
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << on_recv_data_info;
    }

    void RDMASession::real_connecting()
    {
        std::vector<std::unique_ptr<std::thread>> connecting_helper_threads;
        std::atomic_bool ready = {false};
        auto connecting_helper = [](RDMAEndPoint *end_point,
                                    std::atomic_bool &is_ready)
        {
            while (!is_ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            end_point->connecting();
        };
        for (auto &each_endpoint : this->end_point_mgr_)
        {
            std::thread *tmp_thread = new std::thread(connecting_helper,
                                                      each_endpoint.get(),
                                                      std::ref(ready));
            connecting_helper_threads.push_back(std::move(std::unique_ptr<std::thread>(tmp_thread)));
        }
        ready = true;
        for (auto &helper_thread : connecting_helper_threads)
            helper_thread->join();
    }

    void RDMASession::connecting()
    {
        lazy_config_hca();
        real_connecting();
        post_connecting();
    }

}; // end namespace rdma_core