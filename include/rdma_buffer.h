/******************************************************************************
 * Copyright (c) 2021 NEWPLAN, https://chengyang.info
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/

/****************************************************************
 * RDMABuffer is the abstraction of memory for rdma communication
 * it provides basic function as follows:
 *      -- allocate (pinned) physical memory with given size
 *      -- regisger/deregister the buffer into/from RDMAChannel
 *****************************************************************/

#ifndef __RDMA_COMM_CORE_RDMA_BUFFER_H__
#define __RDMA_COMM_CORE_RDMA_BUFFER_H__

#include "rdma_adapter.h"

namespace rdma_core
{
    class RDMAAdapter;

    class RDMABuffer
    {
    public:
        virtual ~RDMABuffer();
        explicit RDMABuffer(                   // construction function of rdmabuffer
            size_t buffer_size_in_byte,        // how many bytes to use in this buffer
            uint32_t num_blocks,               // how many block buffer it would be
            std::string buffer_info,           // buffer info
            RDMABuffer *base_buffer = nullptr, // the base buffer
            bool is_sub_buffer = false);       // whether the buffer is sub-buffer

    public:
        static RDMABuffer *allocate_buffer( // static method to allocate buffers
            size_t buffer_size,             // how many bytes to use in this buffer
            uint32_t num_blocks = 1,        // how many blocks of this buffer would be
            std::string buffer_info = "");  // buffer info

        // register the buffer into the channel,currently can only be accessed by one channel
        virtual bool register_in_channel(RDMAAdapter *channel_);

        // deregister the buffer from channel
        virtual bool deregister_from_channel(RDMAAdapter *channel_);

        // get the buffer size in bytes
        inline size_t size()
        {
            return buffer_size;
        }

        // get the buffer address
        inline uint8_t *data_addr()
        {
            return data_ptr;
        }

        // get the the channel registered in the buffer
        inline RDMAAdapter *get_registered_channel()
        {
            return owned_by_channel;
        }

        // fill in the buffer with given message
        bool fill_in(uint8_t *to_copyed_data, // to fillin the buffer data
                     uint32_t data_length);   // how many bytes the data length to copy

        bool clear(); //clear the buffer

        // security check for the buffer when using
        bool security_check();

        // release the buffer
        bool release();

        // reset the buffer
        bool reset(uint32_t total_size,    // relocate the buffer with given size
                   std::string info = ""); // reset the buffer info

        // show the brief info of this buffer
        inline std::string info()
        {
            return "RDMABuffer(" + buffer_name + ")";
        }

        // the detail info of this buffer
        std::string to_string();
        void print_buffer_info();

        inline RDMABuffer *at(size_t index)
        {
            CHECK(index < blocks_) << "block index exceeds the maximum number of blocks: " << blocks_;
            return buffer_mgr_[index];
        }
        inline uint32_t get_block_size()
        {
            return buffer_mgr_.size();
        }

        inline RDMABuffer *next()
        {
            RDMABuffer *next_buf = base_buffer_->buffer_mgr_[(++base_buffer_->next_active) % base_buffer_->blocks_];
            //CHECK(next_buf->in_used == false) << "In valid use for the next buffer " << buffer_name;
            next_buf->in_used = true;
            return next_buf;
        }
        inline RDMABuffer *last()
        {
            RDMABuffer *last = base_buffer_->buffer_mgr_[(++base_buffer_->last_active) % base_buffer_->blocks_];
            //CHECK(last->in_used == true) << "In valid use for the last buffer " << buffer_name;
            last->in_used = false;
            return last;
        }

    public:
        std::string buffer_name = "";            // buffer info
        size_t buffer_size = 0u;                 // buffer size
        uint8_t *data_ptr = nullptr;             // pointer of mem addr
        RDMAAdapter *owned_by_channel = nullptr; // channel owning this buffer
        bool is_initialized = false;             // is initialized
        struct ibv_mr *mr_ = nullptr;            //register memory region
        int next_active = -1;                    // the active index buffer
        int last_active = -1;                    // the last_active index buffer
        bool in_used = false;                    // whether the buffer is in used

    private:
        int buffer_index = -1;                 // the index of the buffer
        std::vector<RDMABuffer *> buffer_mgr_; // the buffer_mgr
        bool is_sub_buffer_ = false;           // whether the buffer is a sub-buffer
        size_t blocks_ = 1;                    // how many blocks the buffer owns
        RDMABuffer *base_buffer_ = nullptr;    // return the base buffer;
    };

}; // end namespace rdma_core
#endif