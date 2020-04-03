#include <biji.hpp>

#include <atomic>
#include <iostream>

namespace zmq = bc::protocol::zmq;

namespace biji {

std::optional<history_map> get_history(const address_list& addresses)
{
    // Set up the server connection.
    zmq::context context;
    zmq::socket socket(context, zmq::socket::role::dealer);

    std::cout << "Connecting to " << blockchain_server_address
        << "..." << std::endl;
    const auto endpoint = bcs::config::endpoint(blockchain_server_address);

    if (socket.connect(endpoint) != bcs::error::success)
    {
        std::cerr << "Cannot connect to server" << std::endl;
        return std::nullopt;
    }

    std::atomic<bool> is_error = false;

    const auto unknown_handler = [&is_error](const std::string& command)
    {
        std::cout << "unknown command: " << command << std::endl;
        is_error = true;
    };

    const auto error_handler = [&is_error](const bcs::code& code)
    {
        std::cout << "error: " << code.message() << std::endl;
        is_error = true;
    };

    bcc::socket_stream stream(socket);

    // Wait 2 seconds for the connection, with no failure retries.
    bcc::proxy proxy(stream, unknown_handler, 4000, 0);

    std::mutex mutex;
    history_map histories;
    for (const auto& address: addresses)
    {
        const auto completion_handler = [&histories, &mutex, address](
            const history_list& history)
        {
            std::lock_guard<std::mutex> guard(mutex);
            histories[address] = history;
        };

        proxy.blockchain_fetch_history3(error_handler, completion_handler,
            address);
    }

    zmq::poller poller;
    poller.add(socket);

    while (poller.wait(4000).contains(socket.id()))
        stream.read(proxy);

    if (is_error)
        return std::nullopt;

    return histories;
}

} // namespace biji

