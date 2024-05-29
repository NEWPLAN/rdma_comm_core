#include <iostream>

#include "config.h"
#include "rdma_client_sess.h"
#include "rdma_server_sess.h"
#include "util/logging.h"
#include <memory>
#include <thread>

#define B (1)
#define KB (1024 * B)
#define MB (1024 * KB)

#define NUM_BLOCK 128
#define BLOCK_SIZE 16 * KB
#define TRAFFIC_CLASS 0

enum class MsgDataChannel : uint32_t
{
    REQUEST_BUFFER = 1,
    RESPONSE_TO_REQUEST_BUFFER = 2,
    SEND_TEST_REQUEST = 3,
    RESPONSE_TO_SEND_TEST_REQUEST = 4,
    WRITE_TEST_REQUEST = 5,
    RESPONSE_TO_WRITE_TEST_REQUEST = 6,
    READ_TEST_REQUEST = 7,
    RESPONSE_TO_READ_TEST_REQUEST = 8,

    BYEBYE = 255,
    TRANSFER_RAW_DATA = 299
};

using namespace rdma_core;
namespace
{
    class ServerTower : public RDMAServerSession
    {
    public:
        ServerTower(Config &conf) :
            RDMAServerSession(conf)
        {
            conf.session_id = "FullMeshService";
            VLOG(3) << "Creating RDMAServer for FullMesh";
        }
        virtual ~ServerTower()
        {
            VLOG(3) << "Destroying RDMAServer for FullMesh";
        }

    protected:
        virtual void lazy_config_hca()
        {
            // CHECK(end_point_mgr_.size() == 1) << "Invalid configuration for your test";
            for (auto &each_endpoint : end_point_mgr_)
            {
                auto &a_config = each_endpoint->get_channel()->get_config();
                a_config.using_shared_cq = true;
                a_config.max_recv_wr = NUM_BLOCK;
                a_config.max_send_wr = NUM_BLOCK;
                a_config.traffic_class = TRAFFIC_CLASS;
                a_config.cq_key = "GlobalSharedCQForRecv";
                a_config.cq_size = (a_config.max_recv_wr + a_config.max_send_wr) * end_point_mgr_.size();
            }
        }
        virtual void post_connecting()
        {
        }
        virtual void process_CQ(std::vector<RDMAChannel *> aggregated_channels)
        {
            CHECK(aggregated_channels.size() <= 64) << "Currently, one thread only support <=64 connection";

            uint32_t num_channels = aggregated_channels.size();

            VLOG(2) << "This thread holds " << num_channels << " Channels";

            std::vector<std::unique_ptr<RDMABuffer>> tensor_buffer_recv;
            std::vector<std::unique_ptr<RDMABuffer>> tensor_buffer_send;

            for (uint32_t chan_index = 0; chan_index < num_channels; chan_index++)
            {
                RDMABuffer *tmp_buffer;
                tmp_buffer = RDMABuffer::allocate_buffer(BLOCK_SIZE, // 1MB bytes per block
                                                         NUM_BLOCK,  // 128 blocks
                                                         "datachannel_write_placeholder");
                tensor_buffer_send.push_back(std::move(std::unique_ptr<RDMABuffer>(tmp_buffer)));
                aggregated_channels[chan_index]->register_buffer(tmp_buffer); // register send buffer

                tmp_buffer = RDMABuffer::allocate_buffer(BLOCK_SIZE, // 1MB bytes per block
                                                         NUM_BLOCK,  // 128 blocks
                                                         "datachannel_recv_placeholder");
                tensor_buffer_recv.push_back(std::move(std::unique_ptr<RDMABuffer>(tmp_buffer)));
                aggregated_channels[chan_index]->register_buffer(tmp_buffer); // register recv buffer
                aggregated_channels[chan_index]->setup_index_in_session(chan_index);
            }

            for (uint32_t chan_index = 0; chan_index < num_channels; chan_index++)
            {
                for (uint32_t buffer_bin = 0; buffer_bin < NUM_BLOCK; buffer_bin++)
                {
                    RDMAChannel *active_channel = aggregated_channels[chan_index];
                    RDMABuffer *recv_buf = tensor_buffer_recv[chan_index]->next();
                    active_channel->recv_remote(recv_buf, BLOCK_SIZE);
                }
            }

            for (auto &active_channel : aggregated_channels)
            {
                RDMAEndPoint *active_ep = active_channel->get_registered_endpoint();
                active_ep->sync_with_peer("FinalSync_for_Working");
            }

            struct ibv_wc global_wc[128];

            do {
                for (auto *each_channel : aggregated_channels)
                {
                    int num_wqe = each_channel->poll_cq_batch(global_wc, 128);
                    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (num_wqe == 0) continue;
                    VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "The number of polled cqe is " << num_wqe;

                    for (int wqe_index = 0; wqe_index < num_wqe; wqe_index++)
                    {
                        if (global_wc[wqe_index].status != IBV_WC_SUCCESS)
                        {
                            each_channel->show_qp_info();
                            LOG(FATAL) << "[ERROR]: Encounting unsuccess wqe: " << each_channel->info()
                                       << ", for: " << ibv_wc_status_str(global_wc[wqe_index].status);
                        }
                        RDMAChannel *active_channel = (RDMAChannel *)global_wc[wqe_index].wr_id;
                        uint32_t channel_index = active_channel->get_index_in_session();

                        switch (global_wc[wqe_index].opcode)
                        {
                            case IBV_WC_SEND:
                            {
                                active_channel->decrease_sqe();
                                tensor_buffer_send[channel_index]->last(); // ack last
                                VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Send Done";
                                break;
                            }
                            case IBV_WC_RECV:
                            {
                                VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Recv completion";
                                tensor_buffer_recv[channel_index]->last();
                                active_channel->decrease_rqe();

                                MsgDataChannel msg = static_cast<MsgDataChannel>(global_wc[wqe_index].imm_data);
                                if (msg == MsgDataChannel::REQUEST_BUFFER)
                                {
                                    RDMABuffer *active_send_buf = tensor_buffer_send[channel_index]->next();
                                    struct CommDescriptor *local_comm =
                                        (struct CommDescriptor *)active_send_buf->data_ptr;
                                    local_comm->buffer_addr_ = (uint64_t)tensor_buffer_send[channel_index]->data_ptr;
                                    local_comm->buffer_length_ = tensor_buffer_send[channel_index]->buffer_size;
                                    local_comm->rkey_ = tensor_buffer_send[channel_index]->mr_->rkey;
                                    local_comm->fd_ = 0;
                                    active_channel->send_remote(
                                        active_send_buf, sizeof(struct CommDescriptor),
                                        static_cast<uint32_t>(MsgDataChannel::RESPONSE_TO_REQUEST_BUFFER));
                                }
                                else
                                {
                                    UNIMPLEMENTED;
                                }

                                RDMABuffer *next_recv_buffer = tensor_buffer_recv[channel_index]->next();
                                active_channel->recv_remote(next_recv_buffer, BLOCK_SIZE);
                                break;
                            }
                            case IBV_WC_RECV_RDMA_WITH_IMM:
                            {
                                UNIMPLEMENTED;
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
        }
    };

    class ClientTower : public RDMAClientSession
    {
    public:
        ClientTower(Config &conf) :
            RDMAClientSession(conf)
        {
            // session_id_ = "FullMeshService";
            VLOG(3) << "Creating RDMAClient for FullMesh";
        }
        virtual ~ClientTower()
        {
            VLOG(3) << "Destroying RDMAClient for FullMesh";
        }

    protected:
        virtual void lazy_config_hca()
        {
            CHECK(end_point_mgr_.size() == 1) << "Invalid configuration for your test";
            for (auto &each_endpoint : end_point_mgr_)
            {
                auto &a_config = each_endpoint->get_channel()->get_config();
                a_config.using_shared_cq = false;
                a_config.max_recv_wr = NUM_BLOCK;
                a_config.max_send_wr = NUM_BLOCK;
                a_config.traffic_class = TRAFFIC_CLASS;
                // a_config.cq_key = "GlobalSharedCQForRecv";
                a_config.cq_size = (a_config.max_recv_wr + a_config.max_send_wr) * end_point_mgr_.size();
            }
        }
        virtual void post_connecting()
        {
        }
        virtual void process_CQ(std::vector<RDMAChannel *> aggregated_channels)
        {
            CHECK(aggregated_channels.size() == 1) << "Currently, one thread only place 1 connection at client side";

            uint32_t num_channels = aggregated_channels.size();

            CHECK(num_channels == 1) << "Fatal error of creating more than 1 channel in client";

            std::vector<std::unique_ptr<RDMABuffer>> tensor_buffer_recv;
            std::vector<std::unique_ptr<RDMABuffer>> tensor_buffer_send;

            std::vector<struct CommDescriptor> peer_comm_fd_mgr;
            peer_comm_fd_mgr.resize(aggregated_channels.size());

            for (uint32_t chan_index = 0; chan_index < num_channels; chan_index++)
            {
                RDMABuffer *tmp_buffer;
                tmp_buffer = RDMABuffer::allocate_buffer(BLOCK_SIZE, // 1MB bytes per block
                                                         NUM_BLOCK,  // 128 blocks
                                                         "datachannel_write_placeholder");
                tensor_buffer_send.push_back(std::move(std::unique_ptr<RDMABuffer>(tmp_buffer)));
                aggregated_channels[chan_index]->register_buffer(tmp_buffer); // register send buffer

                tmp_buffer = RDMABuffer::allocate_buffer(BLOCK_SIZE, // 1MB bytes per block
                                                         NUM_BLOCK,  // 128 blocks
                                                         "datachannel_recv_placeholder");
                tensor_buffer_recv.push_back(std::move(std::unique_ptr<RDMABuffer>(tmp_buffer)));
                aggregated_channels[chan_index]->register_buffer(tmp_buffer); // register recv buffer
                aggregated_channels[chan_index]->setup_index_in_session(chan_index);
            }

            for (uint32_t chan_index = 0; chan_index < num_channels; chan_index++)
            {
                for (uint32_t buffer_bin = 0; buffer_bin < NUM_BLOCK; buffer_bin++)
                {
                    RDMAChannel *active_channel = aggregated_channels[chan_index];
                    RDMABuffer *recv_buf = tensor_buffer_recv[chan_index]->next();
                    active_channel->recv_remote(recv_buf, BLOCK_SIZE);
                }
            }

            for (auto &active_channel : aggregated_channels)
            {
                RDMAEndPoint *active_ep = active_channel->get_registered_endpoint();
                active_ep->sync_with_peer("FinalSync_for_Working");
            }

            for (auto &active_channel : aggregated_channels)
            {
                int channel_index = active_channel->get_index_in_session();
                RDMABuffer *active_send_buf = tensor_buffer_send[channel_index]->next();
                active_channel->send_remote(active_send_buf, 1, static_cast<uint32_t>(MsgDataChannel::REQUEST_BUFFER));
            }

            struct ibv_wc global_wc[128];

            do {
                for (auto *each_channel : aggregated_channels)
                {
                    int num_wqe = each_channel->poll_cq_batch(global_wc, 128);
                    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (num_wqe == 0) continue;
                    VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "The number of polled cqe is " << num_wqe;

                    for (int wqe_index = 0; wqe_index < num_wqe; wqe_index++)
                    {
                        if (global_wc[wqe_index].status != IBV_WC_SUCCESS)
                        {
                            each_channel->show_qp_info();
                            LOG(FATAL) << "[ERROR]: Encounting unsuccess wqe: " << each_channel->info()
                                       << ", for: " << ibv_wc_status_str(global_wc[wqe_index].status);
                        }
                        RDMAChannel *active_channel = (RDMAChannel *)global_wc[wqe_index].wr_id;
                        uint32_t channel_index = active_channel->get_index_in_session();

                        switch (global_wc[wqe_index].opcode)
                        {
                            case IBV_WC_SEND:
                            {
                                active_channel->decrease_sqe();
                                tensor_buffer_send[channel_index]->last(); // ack last
                                VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Send Done";
                                break;
                            }
                            case IBV_WC_RECV:
                            {
                                active_channel->decrease_rqe();
                                tensor_buffer_recv[channel_index]->last();

                                MsgDataChannel msg = static_cast<MsgDataChannel>(global_wc[wqe_index].imm_data);
                                if (msg == MsgDataChannel::RESPONSE_TO_REQUEST_BUFFER)
                                {
                                    RDMABuffer *active_recv_buf = tensor_buffer_recv[channel_index]->next();
                                    struct CommDescriptor *peer_comm =
                                        (struct CommDescriptor *)active_recv_buf->data_ptr;
                                    peer_comm_fd_mgr[channel_index] = *peer_comm;
                                }
                                else
                                {
                                    UNIMPLEMENTED;
                                }
                                for (uint32_t bin_index = 0; bin_index < NUM_BLOCK - 1; bin_index++)
                                { // write to peer
                                    RDMABuffer *next_send_buf = tensor_buffer_send[channel_index]->next();
                                    // next_send_buf = tensor_buffer_send[channel_index]->at(0);
                                    active_channel->write_remote(
                                        next_send_buf, next_send_buf->buffer_size, &peer_comm_fd_mgr[channel_index],
                                        static_cast<uint32_t>(MsgDataChannel::TRANSFER_RAW_DATA));
                                }
                                break;
                            }
                            case IBV_WC_RECV_RDMA_WITH_IMM:
                            {
                                UNIMPLEMENTED;
                                break;
                            }
                            case IBV_WC_RDMA_WRITE:
                            {
                                VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N / 100) << "WRITE completion";
                                active_channel->decrease_sqe();

                                tensor_buffer_send[channel_index]->last(); // ack last

                                RDMABuffer *next_send_buf = tensor_buffer_send[channel_index]->next();
                                // next_send_buf = tensor_buffer_send[channel_index]->at(0);
                                active_channel->write_remote(next_send_buf, next_send_buf->buffer_size,
                                                             &peer_comm_fd_mgr[channel_index],
                                                             static_cast<uint32_t>(MsgDataChannel::TRANSFER_RAW_DATA));
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
        }
    };
}; // namespace

void start_server_service(Config conf)
{
    conf.role = "master";
    conf.master_ip = "";
    std::unique_ptr<RDMASession> server_sess_mgr_;
    conf.serve_as_client = false;
    server_sess_mgr_.reset(new ServerTower(conf));
    server_sess_mgr_->init_session();
    server_sess_mgr_->connecting();
    server_sess_mgr_->running();
}

void start_client_service(Config conf, std::string server_ip)
{
    conf.role = "slaver";
    conf.master_ip = server_ip;
    conf.cluster.clear();
    std::unique_ptr<RDMASession> client_sess_mgr_;
    conf.serve_as_client = true;
    client_sess_mgr_.reset(new ClientTower(conf));
    client_sess_mgr_->init_session();
    client_sess_mgr_->connecting();
    client_sess_mgr_->running();
}
#include <unistd.h>

int main(int argc, char *argv[])
{
    Derived_Config conf;
    conf.parse_args(argc, argv);

    VLOG(3) << "Start mesh comm_service";
    //  /*
    std::vector<std::thread> service_groups;
    service_groups.push_back(std::thread(start_server_service, conf));
    for (auto &server_ip : conf.cluster)
    {
        VLOG(3) << "Connecting to " << server_ip;
        service_groups.push_back(std::thread(start_client_service, conf, server_ip));
    }

    for (auto &each_thread : service_groups) { each_thread.join(); }

    /*

    std::unique_ptr<RDMASession> server_sess_mgr_;
    std::unique_ptr<RDMASession> client_sess_mgr_;

    if (conf.role == "master")
    {

        conf.serve_as_client = false;
        server_sess_mgr_.reset(new ServerTower(conf));
        server_sess_mgr_->init_session();
        server_sess_mgr_->connecting();
        server_sess_mgr_->running();
    }
    else if (conf.role == "slaver")
    {
        conf.serve_as_client = true;
        client_sess_mgr_.reset(new ClientTower(conf));
        client_sess_mgr_->init_session();
        client_sess_mgr_->connecting();
        client_sess_mgr_->running();
    }
    else
    {
        LOG(FATAL) << "Unknown role for: " << conf.role;
    }
    */

    return 0;
}
