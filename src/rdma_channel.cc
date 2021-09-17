#include "rdma_channel.h"
#include "util/logging.h"

namespace rdma_core
{
    RDMAChannel::RDMAChannel(Config &con, std::string id, RDMAEndPoint *owned_ep) :
        RDMAAdapter(id), work_env_(con), registered_endpoint_(owned_ep)
    {
        TRACE_IN;
        VLOG(3) << "Creating RDMAChannel with id: " << id;
        TRACE_OUT;
    }

    RDMAChannel::~RDMAChannel()
    {
        TRACE_IN;
        // VLOG(3) << "[TRACING-IN] function \"" << __FUNCTION__ << "\": size = " << owning_buffers.size();
        // UNIMPLEMENTED;
        for (auto &each_buffer : owning_buffers)
        {
            //  VLOG(3) << "[Before] Deregister buffer: " << each_buffer.first << " from " << this;
            each_buffer.second->deregister_from_channel(this);
            //  VLOG(3) << "[After] Deregister buffer: " << each_buffer.first << " from " << this;
        }
        VLOG(3) << "Destroying RDMAChannel with id: " << id_;
        TRACE_OUT;
        // VLOG(3) << "[TRACING-OUT] function \"" << __FUNCTION__ << "\"";
    }

    bool RDMAChannel::register_buffer(RDMABuffer *buffer)
    {
        TRACE_IN;
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";
        CHECK(owning_buffers.find(buffer->buffer_name) == owning_buffers.end())
            << RLOG::make_string("The buffer (%s) has already "
                                 "been registered in this channel (%s)",
                                 buffer->buffer_name.c_str(), info().c_str());

        VLOG(3) << "Registering buffer (name = " << buffer->buffer_name
                << ", size = " << buffer->buffer_size << ") into " << info();

        buffer->register_in_channel(this);

        owning_buffers.insert({buffer->buffer_name, buffer});
        TRACE_OUT;
        return true;
    }

    bool RDMAChannel::remove_buffer(RDMABuffer *buffer)
    {
        TRACE_IN;
        // UNIMPLEMENTED;
        CHECK(buffer != 0) << "RDMABuffer has not been initialized";

        if (0 == owning_buffers.erase(buffer->buffer_name))
        { // Returns 0 if key not found)
            LOG(WARNING) << "Buffer " << buffer->buffer_name
                         << " is not register in channel " << info();
            TRACE_OUT;
            return false;
        }

        VLOG(3) << "Buffer(" << buffer->buffer_name
                << ") has been removed from " << info();
        TRACE_OUT;
        return false;
    }

    RDMABuffer *RDMAChannel::find_buffer(std::string key)
    {
        TRACE_IN;
        auto expected_buf = owning_buffers.find(key);
        if (expected_buf == owning_buffers.end())
        {
            LOG(WARNING) << "Cannot find the buffer of " << key;
            TRACE_OUT;
            return nullptr;
        }
        TRACE_OUT;
        return expected_buf->second;
    }

    bool RDMAChannel::insert_peer_buffer(std::string key,
                                         struct CommDescriptor &peer_)
    {
        TRACE_IN;
        auto expected_buf = peer_buffer_mgr_.find(key);
        if (expected_buf != peer_buffer_mgr_.end())
        {
            LOG(WARNING) << "the peer buffer of " << key
                         << " has been inserted into " << info();
            TRACE_OUT;
            return false;
        }
        peer_buffer_mgr_.insert({key, peer_});
        TRACE_OUT;
        return true;
    }
    struct CommDescriptor *RDMAChannel::find_peer_buffer(std::string key)
    {
        TRACE_IN;
        auto expected_buf = peer_buffer_mgr_.find(key);
        if (expected_buf == peer_buffer_mgr_.end())
        {
            LOG(WARNING) << "Cannot find the peer buffer of " << key;
            return nullptr;
        }
        TRACE_OUT;
        return &(expected_buf->second);
    }

    std::string RDMAChannel::get_id()
    {
        return "(" + id_ + ")" + registered_endpoint_->get_id();
    }

}; // end namespace rdma_core