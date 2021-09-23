
#include "config.h"
#include "rdma_channel.h"
#include "rdma_client_sess.h"
#include "rdma_server_sess.h"
#include "util/logging.h"

#include "rdma_buffer.h"

#include "util/time_record.h"
#include <chrono>
#include <thread>
#define B (1)
#define KB (1024 * B)
#define MB (1024 * KB)

enum class MessageforTest : uint32_t
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
    TEST_RAW_DATA = 299
};

using namespace rdma_core;
class SchedulingClient : public RDMAClientSession
{
private:
    RDMAChannel *active_channel = nullptr;
    RDMABuffer *benchmark_buffer = nullptr;
    struct ibv_wc global_wc[128];
    struct CommDescriptor buffer_peer;

    void register_buffer(RDMAChannel *active_channel_)
    {
        TRACE_IN;
        CHECK(active_channel_ != nullptr);
        { // register memory buffer for test
            active_channel = active_channel_;
            benchmark_buffer = RDMABuffer::allocate_buffer(8 * MB, 1000,
                                                           "benchmark_buffer");
            active_channel->register_buffer(benchmark_buffer);
        }

        { // post recv here to recv buffer key
            RDMABuffer *buffer_recv = benchmark_buffer->at(0);
            active_channel->recv_remote(buffer_recv, buffer_recv->buffer_size);
        }
        VLOG(0) << "Register buffer for test@" << info();
        TRACE_OUT;
    }
    void exchange_buffer_info()
    {
        TRACE_IN;
        // request exchange info
        RDMABuffer *buffer_to_send = benchmark_buffer->at(1);
        active_channel->send_remote(buffer_to_send, 2,
                                    static_cast<uint32_t>(MessageforTest::REQUEST_BUFFER));
        int num_wqe = 0;
        bool send_completion = false;
        bool recv_completion = false;
        for (;;)
        {
            if (send_completion && recv_completion)
                break;
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            if (global_wc[0].opcode == IBV_WC_SEND)
            {
                CHECK(send_completion == false)
                    << "Unexpected send completion events";
                send_completion = true;
                continue;
            }
            CHECK(global_wc[0].opcode == IBV_WC_RECV)
                << "Receive unexpected opcode";
            CHECK(static_cast<MessageforTest>(global_wc[0].imm_data) == MessageforTest::RESPONSE_TO_REQUEST_BUFFER)
                << "Unexpected message for the test benchmark";
            CHECK(recv_completion == false) << "Unexpected receive completion events";
            recv_completion = true;
        }
        // loading peer buffer
        buffer_peer = *(struct CommDescriptor *)benchmark_buffer->at(0)->data_ptr;
        VLOG(2) << "Exchange buffer Done";
        TRACE_OUT;
    }

    void lat_send(size_t msg_length)
    {
        TRACE_IN;
        active_channel->recv_remote(benchmark_buffer->at(0), 64);
        // request exchange info
        RDMABuffer *buffer_to_send = benchmark_buffer->at(1);
        active_channel->send_remote(buffer_to_send, 2,
                                    static_cast<uint32_t>(MessageforTest::SEND_TEST_REQUEST));
        int num_wqe = 0;
        bool send_completion = false;
        bool recv_completion = false;
        for (;;)
        {
            if (send_completion && recv_completion)
                break;
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            if (global_wc[0].opcode == IBV_WC_SEND)
            {
                CHECK(send_completion == false)
                    << "Unexpected send completion events";
                send_completion = true;
                continue;
            }
            CHECK(global_wc[0].opcode == IBV_WC_RECV)
                << "Receive unexpected opcode";
            CHECK(static_cast<MessageforTest>(global_wc[0].imm_data) == MessageforTest::RESPONSE_TO_SEND_TEST_REQUEST)
                << "Unexpected message for the test benchmark";
            CHECK(recv_completion == false) << "Unexpected receive completion events";
            recv_completion = true;
        }
        newplan::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timer.Start();
        for (int index = 0; index < 1000; index++)
        {
            active_channel->send_remote(benchmark_buffer->next(), msg_length,
                                        static_cast<uint32_t>(MessageforTest::TEST_RAW_DATA));
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            CHECK(global_wc[0].opcode == IBV_WC_SEND) << "Unexpected opcode";
            benchmark_buffer->last();
        }
        timer.Stop();
        std::string format;
        if (msg_length < KB)
            format = std::to_string(msg_length);
        else if (msg_length < MB)
            format = std::to_string(msg_length / KB) + " K";
        else
            format = std::to_string(msg_length / MB) + " M";
        VLOG(2) << "Latency for send (" << format << " bytes): " << timer.MicroSeconds() / 1000.0 << " us/op";
        TRACE_OUT;
    }

    void write_test(size_t msg_length)
    {
        TRACE_IN;
        int num_wqe = 0;
        newplan::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timer.Start();
        for (int index = 0; index < 1000; index++)
        {
            active_channel->write_remote(benchmark_buffer->next(),
                                         msg_length,
                                         &buffer_peer,
                                         static_cast<uint32_t>(MessageforTest::TEST_RAW_DATA));
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            CHECK(global_wc[0].opcode == IBV_WC_RDMA_WRITE) << "Unexpected opcode";
            benchmark_buffer->last();
        }
        timer.Stop();
        std::string format;
        if (msg_length < KB)
            format = std::to_string(msg_length);
        else if (msg_length < MB)
            format = std::to_string(msg_length / KB) + " K";
        else
            format = std::to_string(msg_length / MB) + " M";
        VLOG(2) << "Latency for write (" << format << " bytes): " << timer.MicroSeconds() / 1000.0 << " us/op";
        TRACE_OUT;
    }

    void write_bw_test(size_t msg_length)
    {
        TRACE_IN;
        int num_wqe = 0;
        newplan::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timer.Start();
        for (int index = 0; index < 1000; index++)
        {
            active_channel->write_remote(benchmark_buffer->next(), //next
                                         msg_length,
                                         &buffer_peer,
                                         static_cast<uint32_t>(MessageforTest::TEST_RAW_DATA));
        }
        int confirm = 0;
        for (int index = 0; index < 1000; index++)
        {
            if (confirm >= 1000)
                break;
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 128);
            } while (num_wqe == 0);
            for (int wc_index = 0; wc_index < num_wqe; wc_index++)
            {
                CHECK(global_wc[wc_index].status == IBV_WC_SUCCESS)
                    << "Failed to query exchange info with error "
                    << ibv_wc_status_str(global_wc[wc_index].status);
                CHECK(global_wc[wc_index].opcode == IBV_WC_RDMA_WRITE) << "Unexpected opcode";
                benchmark_buffer->last();
                confirm++;
            }
        }
        timer.Stop();
        CHECK(confirm == 1000);
        std::string format;
        if (msg_length < KB)
            format = std::to_string(msg_length);
        else if (msg_length < MB)
            format = std::to_string(msg_length / KB) + " K";
        else
            format = std::to_string(msg_length / MB) + " M";
        VLOG(2) << "BW for write (" << format << " bytes): " << 8 * (1000.0 * msg_length) / timer.MicroSeconds() / 1000 << " Gbps";
        TRACE_OUT;
    }

    void write_bw_test_fake(size_t msg_length)
    {
        TRACE_IN;
        int num_wqe = 0;
        newplan::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timer.Start();
        for (int index = 0; index < 1000; index++)
        {
            active_channel->write_remote(benchmark_buffer->at(0), //next
                                         msg_length,
                                         &buffer_peer,
                                         static_cast<uint32_t>(MessageforTest::TEST_RAW_DATA));
        }
        int confirm = 0;
        for (int index = 0; index < 1000; index++)
        {
            if (confirm >= 1000)
                break;
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 128);
            } while (num_wqe == 0);
            for (int wc_index = 0; wc_index < num_wqe; wc_index++)
            {
                CHECK(global_wc[wc_index].status == IBV_WC_SUCCESS)
                    << "Failed to query exchange info with error "
                    << ibv_wc_status_str(global_wc[wc_index].status);
                CHECK(global_wc[wc_index].opcode == IBV_WC_RDMA_WRITE) << "Unexpected opcode";
                //benchmark_buffer->last();
                confirm++;
            }
        }
        timer.Stop();
        CHECK(confirm == 1000);
        std::string format;
        if (msg_length < KB)
            format = std::to_string(msg_length);
        else if (msg_length < MB)
            format = std::to_string(msg_length / KB) + " K";
        else
            format = std::to_string(msg_length / MB) + " M";
        VLOG(2) << "BW for write (" << format << " bytes): " << 8 * (1000.0 * msg_length) / timer.MicroSeconds() / 1000 << " Gbps";
        TRACE_OUT;
    }

    void read_test(size_t msg_length)
    {
        TRACE_IN;
        int num_wqe = 0;
        newplan::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timer.Start();
        for (int index = 0; index < 1000; index++)
        {
            active_channel->read_remote(benchmark_buffer->next(),
                                        msg_length,
                                        &buffer_peer);
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            CHECK(global_wc[0].opcode == IBV_WC_RDMA_READ) << "Unexpected opcode";
            benchmark_buffer->last();
        }
        timer.Stop();
        std::string format;
        if (msg_length < KB)
            format = std::to_string(msg_length);
        else if (msg_length < MB)
            format = std::to_string(msg_length / KB) + " K";
        else
            format = std::to_string(msg_length / MB) + " M";
        VLOG(2) << "Latency for read (" << format << " bytes): " << timer.MicroSeconds() / 1000.0 << " us/op";
        TRACE_OUT;
    }

public:
    SchedulingClient(Config &conf) :
        RDMAClientSession(conf)
    {
        VLOG(3) << "Creating SchedulingClient";
    }
    virtual ~SchedulingClient()
    {
        VLOG(3) << "Destroying SchedulingClient";
    }

    // a loop to processing complement queue events
    virtual void process_CQ(std::vector<RDMAChannel *> aggregated_channels)
    {
        CHECK(aggregated_channels.size() == 1)
            << "Invalid aggregated_channels for this task";

        this->register_buffer(aggregated_channels[0]);

        { // syn for test start
            aggregated_channels[0]->get_registered_endpoint()->sync_with_peer("benchmark test start");
        }
        exchange_buffer_info();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lat_send(buffer_size);
        }

        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            write_test(buffer_size);
        }
        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            read_test(buffer_size);
        }

        VLOG(0) << "Using round-robin data buffer for BW test";
        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            write_bw_test(buffer_size);
        }
        // using fake data(only one piece of data buffer)
        VLOG(0) << "Using one data buffer for BW test";
        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            write_bw_test_fake(buffer_size);
        }

        active_channel->send_remote(benchmark_buffer->at(0), 64,
                                    static_cast<uint32_t>(MessageforTest::BYEBYE));
    }
    virtual void lazy_config_hca()
    {
        CHECK(end_point_mgr_.size() == 1) << "Invalid configuration for your test";
        for (auto &each_endpoint : end_point_mgr_)
        {
            auto &a_config = each_endpoint->get_channel()->get_config();
            a_config.using_shared_cq = false;
            a_config.max_recv_wr = 1024;
            a_config.max_send_wr = 1024;
            a_config.cq_size = a_config.max_recv_wr + a_config.max_send_wr;
        }
    } // hook before the connecting endpoint
    virtual void post_connecting()
    {
        //UNIMPLEMENTED;
    } // hook after endpoint is connected
};

class SchedulingServer : public RDMAServerSession
{
private:
    RDMAChannel *active_channel = nullptr;
    RDMABuffer *benchmark_buffer = nullptr;
    struct ibv_wc global_wc[128];
    std::mutex m_mutex;

    void register_buffer(RDMAChannel *active_channel_)
    {
        CHECK(active_channel_ != nullptr);
        { // register memory buffer for test
            active_channel = active_channel_;
            benchmark_buffer = RDMABuffer::allocate_buffer(8 * MB, 1000,
                                                           "benchmark_buffer");
            active_channel->register_buffer(benchmark_buffer);
        }

        { // post recv here to recv buffer key
            RDMABuffer *buffer_recv = benchmark_buffer->at(0);
            active_channel->recv_remote(buffer_recv, buffer_recv->buffer_size);
        }
        VLOG(0) << "Register buffer for test@" << info();
    }

    void exchange_buffer_info()
    {
        int num_wqe = 0;
        do
        {
            num_wqe = active_channel->poll_cq_batch(global_wc, 1);
        } while (num_wqe == 0);

        CHECK(global_wc[0].status == IBV_WC_SUCCESS)
            << "Failed to query exchange info with error "
            << ibv_wc_status_str(global_wc[0].status);
        CHECK(global_wc[0].opcode == IBV_WC_RECV)
            << "Receive unexpected opcode";
        CHECK(static_cast<MessageforTest>(global_wc[0].imm_data) == MessageforTest::REQUEST_BUFFER)
            << "Unexpected message for the exchange buffer";

        // loading my buffer to exchange
        struct CommDescriptor *mybuffer = (struct CommDescriptor *)benchmark_buffer->at(1)->data_ptr;
        RDMABuffer *buffer_to_write = benchmark_buffer->at(0);
        mybuffer->buffer_addr_ = (uint64_t)buffer_to_write->data_ptr;
        mybuffer->buffer_length_ = buffer_to_write->buffer_size;
        mybuffer->rkey_ = buffer_to_write->mr_->rkey;
        // response to  exchange info
        active_channel->send_remote(benchmark_buffer->at(1), 64,
                                    static_cast<uint32_t>(MessageforTest::RESPONSE_TO_REQUEST_BUFFER));

        do
        {
            num_wqe = active_channel->poll_cq_batch(global_wc, 1);
        } while (num_wqe == 0);

        CHECK(global_wc[0].status == IBV_WC_SUCCESS)
            << "Failed to query exchange info with error "
            << ibv_wc_status_str(global_wc[0].status);
        CHECK(global_wc[0].opcode == IBV_WC_SEND)
            << "Receive unexpected opcode";

        VLOG(2) << "Exchange buffer Done";
    }

    void lat_send()
    {
        active_channel->recv_remote(benchmark_buffer->at(0), 64);
        int num_wqe = 0;
        do
        {
            num_wqe = active_channel->poll_cq_batch(global_wc, 1);
        } while (num_wqe == 0);

        CHECK(global_wc[0].status == IBV_WC_SUCCESS)
            << "Failed to query exchange info with error "
            << ibv_wc_status_str(global_wc[0].status);
        CHECK(global_wc[0].opcode == IBV_WC_RECV)
            << "Receive unexpected opcode";

        for (int index = 0; index < 1000; index++)
        {
            RDMABuffer *buffer_bin = benchmark_buffer->next();
            active_channel->recv_remote(buffer_bin, buffer_bin->buffer_size);
        }
        active_channel->send_remote(benchmark_buffer->at(1), 0,
                                    static_cast<uint32_t>(MessageforTest::RESPONSE_TO_SEND_TEST_REQUEST));
        do
        {
            num_wqe = active_channel->poll_cq_batch(global_wc, 1);
        } while (num_wqe == 0);

        CHECK(global_wc[0].status == IBV_WC_SUCCESS)
            << "Failed to query exchange info with error "
            << ibv_wc_status_str(global_wc[0].status);
        CHECK(global_wc[0].opcode == IBV_WC_SEND)
            << "Receive unexpected opcode";

        for (int index = 0; index < 1000; index++)
        {
            do
            {
                num_wqe = active_channel->poll_cq_batch(global_wc, 1);
            } while (num_wqe == 0);

            CHECK(global_wc[0].status == IBV_WC_SUCCESS)
                << "Failed to query exchange info with error "
                << ibv_wc_status_str(global_wc[0].status);
            CHECK(global_wc[0].opcode == IBV_WC_RECV)
                << "Receive unexpected opcode";
            CHECK(static_cast<MessageforTest>(global_wc[0].imm_data) == MessageforTest::TEST_RAW_DATA)
                << "Invalid msg_type";
            benchmark_buffer->last();
        }
    }

public:
    SchedulingServer(Config &conf) :
        RDMAServerSession(conf)
    {
        VLOG(3) << "Creating SchedulingServer";
    }
    virtual ~SchedulingServer()
    {
        VLOG(3) << "Destroy SchedulingServer";
    }

    // a loop to processing complement queue events
    virtual void process_CQ(std::vector<RDMAChannel *> aggregated_channels)
    {
        CHECK(aggregated_channels.size() == 1) << "Invalid aggregated_channels for this task";
        std::lock_guard<std::mutex> lock(m_mutex);

        this->register_buffer(aggregated_channels[0]);
        {
            aggregated_channels[0]->get_registered_endpoint()->sync_with_peer("benchmark test");
        }

        exchange_buffer_info();

        for (uint32_t buffer_size = 1; buffer_size <= 8 * MB; buffer_size *= 2)
        {
            lat_send();
        }

        active_channel->recv_remote(benchmark_buffer->at(0), 64);
        int num_wqe = 0;
        do
        {
            num_wqe = active_channel->poll_cq_batch(global_wc, 1);
        } while (num_wqe == 0);

        CHECK(global_wc[0].status == IBV_WC_SUCCESS)
            << "Failed to query exchange info with error "
            << ibv_wc_status_str(global_wc[0].status);
        CHECK(global_wc[0].opcode == IBV_WC_RECV)
            << "Receive unexpected opcode";
        CHECK(static_cast<MessageforTest>(global_wc[0].imm_data) == MessageforTest::BYEBYE)
            << "Unexpected message for the test benchmark";
    }
    virtual void lazy_config_hca()
    {
        //CHECK(end_point_mgr_.size() == 1) << "Invalid configuration for server";
        for (auto &each_endpoint : end_point_mgr_)
        {
            auto &a_config = each_endpoint->get_channel()->get_config();
            a_config.using_shared_cq = false;
            a_config.max_recv_wr = 1024;
            a_config.max_send_wr = 1024;
            a_config.cq_size = a_config.max_recv_wr + a_config.max_send_wr;
            //a_config.traffic_class = 64;
        }
    } // hook before the connecting endpoint
    virtual void post_connecting()
    {
        // UNIMPLEMENTED;
    } // hook after endpoint is connected
};

#include "config.h"
#include "rdma_session.h"
#include "util/logging.h"
#include <iostream>

int main(int argc, char *argv[])
{
    Derived_Config conf;
    conf.parse_args(argc, argv);

    std::unique_ptr<rdma_core::RDMASession> session = nullptr;
    if (conf.role == "master")
    {
        conf.serve_as_client = false;
        session.reset(new SchedulingServer(conf));
    }
    else if (conf.role == "slaver")
    {
        conf.serve_as_client = true;
        session.reset(new SchedulingClient(conf));
    }
    else
    {
        LOG(FATAL) << "Unknown role for: " << conf.role;
    }
    // session->register_event_callback([]()
    //                                  { VLOG(3) << "Hello world"; },
    //                                  nullptr,
    //                                  nullptr,
    //                                  nullptr,
    //                                  nullptr,
    //                                  nullptr);
    session->init_session();
    session->connecting();
    session->running();

    return 0;
}