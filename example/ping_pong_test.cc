
#include "util/logging.h"

#include "config.h"

#include <iostream>

#include "rdma_session.h"

#include "rdma_client_sess.h"
#include "rdma_server_sess.h"

int main(int argc, char *argv[])
{
    Derived_Config conf;
    conf.parse_args(argc, argv);

    std::unique_ptr<rdma_core::RDMASession> session = nullptr;
    if (conf.role == "master")
    {
        conf.serve_as_client = false;
        session.reset(new rdma_core::RDMAServerSession(conf));
    }
    else if (conf.role == "slaver")
    {
        conf.serve_as_client = true;
        session.reset(new rdma_core::RDMAClientSession(conf));
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