

#include "config.h"
#include "util/logging.h"

#include "rdma_buffer.h"

#include <stdlib.h>

// https://libs.garden/cpp/search?q=rdma&page=2

#define HUGEPAGE_ALIGN (2 * 1024 * 1024)
#define SHMAT_ADDR (void *)(0x0UL)
#define SHMAT_FLAGS (0)
// hugepage configure: https://docs.01.org/clearlinux/latest/guides/maintenance/configure-hugepages.html

namespace rdma_core
{
    bool RDMABuffer::register_in_channel(RDMAAdapter *channel_)
    {
        TRACING("");
        CHECK(!is_sub_buffer_) << "[Error]: sub-buffer cannot register in channel";
        owned_by_channel = channel_;
        this->mr_ = channel_->register_mem(data_ptr, buffer_size,
                                           0, buffer_name);
        for (auto *each_block : buffer_mgr_) //update the buffer mgr
        {
            each_block->owned_by_channel = owned_by_channel;
            each_block->mr_ = this->mr_;
        }
        return true;
    }
    bool RDMABuffer::deregister_from_channel(RDMAAdapter *channel_)
    {
        TRACE_IN;
        if (channel_ == nullptr)
        {
            LOG(WARNING) << "The channel is empty";
            return false;
        }
        //VLOG(3) << "[TRACING-IN] function \"" << __FUNCTION__ << "\"";
        CHECK(!is_sub_buffer_) << "Subbuffer cannot register in channel";

        CHECK(mr_ != nullptr) << "Illegal mr for " << buffer_name;
        channel_->deregister_mem(mr_, buffer_name);
        channel_->remove_buffer(this);

        owned_by_channel = nullptr;
        mr_ = nullptr;
        TRACE_OUT;
        //VLOG(3) << "[TRACING-OUT] function \"" << __FUNCTION__ << "\"";
        return false;
    }

    bool RDMABuffer::security_check()
    {
        TRACE_IN;
        CHECK(buffer_size != 0) << "[ERROR]: buffer size is 0";
        CHECK(data_ptr != nullptr) << "[ERROR]: buffer data_ptr is nullptr";
        CHECK(owned_by_channel != nullptr) << "[ERROR]: buffer owned_by_channel is nullptr";
        TRACE_OUT;
        return true;
    }

    bool RDMABuffer::release()
    {
        TRACE_IN;
        VLOG(3) << "Rleasing buffer : " << buffer_name;

        LOG(FATAL) << __FUNCTION__ << " is not implemented yet";
        TRACE_OUT;
        return false;
    }
    bool RDMABuffer::reset(uint32_t total_size, std::string buffer_info)
    {
        TRACE_IN;
        buffer_name = buffer_info;
        VLOG(3) << "reset the buffer : " << buffer_name;

        CHECK(total_size > 0) << "The buffer should not been empty";

        LOG(FATAL) << __FUNCTION__ << " is not implemented yet";
        is_initialized = true;
        TRACE_OUT;
        return false;
    }

    RDMABuffer::~RDMABuffer()
    {
        TRACE_IN;

        if (!is_sub_buffer_)
        {
            if (this->owned_by_channel != nullptr)
            {
                VLOG(2) << "Deregister buffer";
                this->deregister_from_channel(this->owned_by_channel);
            }
            VLOG(2) << "Clean sub-buffer for " << buffer_name;
            for (auto *each_buff : buffer_mgr_)
            {
                each_buff->owned_by_channel = nullptr;
                each_buff->data_ptr = nullptr;
                each_buff->mr_ = nullptr;
                each_buff->is_initialized = false;
            }
            free((void *)data_ptr);
            this->data_ptr = nullptr;
            this->mr_ = nullptr;
            this->is_initialized = false;
        }
        VLOG(3) << "Releasing buffer: " << buffer_name;

        TRACE_OUT;
    }

    RDMABuffer::RDMABuffer(size_t buffer_size_in_byte, uint32_t num_blocks,
                           std::string buffer_info, RDMABuffer *base_buffer,
                           bool is_sub_buffer) :
        buffer_name(buffer_info),
        buffer_size((size_t)buffer_size_in_byte * (size_t)num_blocks),
        is_sub_buffer_(is_sub_buffer), blocks_(num_blocks)
    {
        TRACING("Creating RDMABuffer");
        if (is_sub_buffer_)
        { // the sub buffer
            CHECK(base_buffer != nullptr) << "error of the base buffer";
            base_buffer_ = base_buffer;
            buffer_index = base_buffer->get_block_size();
            data_ptr = base_buffer_->data_ptr + buffer_index * buffer_size_in_byte;
            owned_by_channel = base_buffer_->owned_by_channel;
            is_initialized = true;
            mr_ = base_buffer_->mr_;
            //VLOG(3) << "Spliting the buffer into " << buffer_index << "from " << base_buffer_;
        }
        else
        {
            VLOG(3) << "Creating buffer : " << buffer_name;
            CHECK(buffer_size > 0) << "The buffer should not been empty";
            CHECK(buffer_size > 0) << "Error of allocating mem buf with size: "
                                   << buffer_size;

            int ret = 0;
            if (0 != (ret = posix_memalign((void **)&data_ptr, sysconf(_SC_PAGESIZE), buffer_size)))
                LOG(FATAL) << "[error] posix_memalign: " << strerror(ret);

            LOG(INFO) << RLOG::make_string("[OK] allocating mem for (%s):  @%p, with size: %lu",
                                           buffer_name.c_str(), data_ptr, buffer_size);

            { // split the buffer
                VLOG(3) << "Spliting the buffer " << buffer_name << " into "
                        << num_blocks << " piece(s)";
                for (uint32_t b_index = 0; b_index < num_blocks; b_index++)
                {
                    std::string sub_buffer_name = std::to_string(b_index) + "@" + buffer_info;
                    buffer_mgr_.push_back(new RDMABuffer(buffer_size_in_byte,
                                                         1,
                                                         sub_buffer_name,
                                                         this,
                                                         true));
                }
            }

            memset(data_ptr, 0, buffer_size_in_byte); //clean all the mem buf
            base_buffer_ = this;
        }

        is_initialized = true;
        //TRACE_OUT;
    }

    RDMABuffer *RDMABuffer::allocate_buffer(size_t buffer_size,
                                            uint32_t num_blocks,
                                            std::string buffer_info)
    {
        return new RDMABuffer(buffer_size, num_blocks, buffer_info);
    }

    std::string RDMABuffer::to_string()
    {
        TRACE_IN;
        std::string tmp_res;
        tmp_res += RLOG::make_string("Buffer Name: %s; Buffer Addr: %p; Buffer size: %u bytes",
                                     buffer_name.c_str(), data_ptr, buffer_size);

        tmp_res += RLOG::make_string("; Register in Channel (%p) ", owned_by_channel);
        if (owned_by_channel != nullptr)
            tmp_res += owned_by_channel->info();
        tmp_res += RLOG::make_string("; Register in NIC (%p) ", mr_);

        if (mr_ != nullptr)
            tmp_res += RLOG::make_string("rkey=(0x%x), lkey=(0x%x) ", mr_->rkey, mr_->lkey);
        TRACE_OUT;
        return tmp_res;
    }

    void RDMABuffer::print_buffer_info()
    {
        TRACE_IN;
        std::string my_info = to_string();
        std::string base_info = base_buffer_->to_string();

        VLOG(3) << "Show the buffer info: " << my_info << " from " << base_info;
        TRACE_OUT;
    }

    bool RDMABuffer::fill_in(uint8_t *to_copyed_data, // to fillin the buffer data
                             uint32_t data_length)
    {
        TRACE_IN;
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "fill_in the buffer with length: " << data_length;
        CHECK(data_length <= buffer_size)
            << "Error, the data length cannot be larger than the whole buffer size."
            << ", Data length = " << data_length << ", Buffer info: " << info();
        clear();
        memcpy(data_ptr, to_copyed_data, data_length);
        TRACE_OUT;
        return true;
    }
    bool RDMABuffer::clear() //clear the buffer
    {
        TRACE_IN;
        VLOG_EVERY_N(3, SHOWN_LOG_EVERN_N) << "Clear the buffer";
        memset(data_ptr, 0, buffer_size);
        TRACE_OUT;
        return true;
    }
}; // end namespace rdma_core