/*
 * socket_ConnectionListener.cpp
 */

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "logging/log_Logger.h"

#include "socket_SocketDriver.h"
#include "socket_PlainRawSocketConnection.h"
#include "socket_ConnectionListener.h"
#include "socket_SocketClientConnection.h"

namespace mutgos
{
namespace socket
{
    // ----------------------------------------------------------------------
    ConnectionListener::ConnectionListener(
        mutgos::socket::SocketDriver *driver,
        boost::asio::io_context &context,
        boost::asio::ip::tcp::endpoint endpoint)
      : initialization_error(false),
        driver_ptr(driver),
        socket_acceptor(context),
        io_context(context),
        socket(context)
    {
        if (not driver_ptr)
        {
            LOG(fatal, "socket", "ConnectionListener",
                "driver_ptr pointer is null!  Crash will follow...");
        }

        boost::system::error_code error_code;

        // Open the acceptor
        //
        socket_acceptor.open(endpoint.protocol(), error_code);

        if (error_code)
        {
            LOG(error, "socket", "ConnectionListener",
                "Failed to open acceptor due to error: "
                + error_code.message());
            initialization_error = true;
            return;
        }

        // Allow address reuse
        //
        try
        {
            socket_acceptor.set_option(
                boost::asio::socket_base::reuse_address(true));
        }
        catch (...)
        {
            LOG(error, "socket", "ConnectionListener",
                "Failed to set reuse_address option to true.");
            initialization_error = true;
            return;
        }

        // Bind to the server address
        //
        socket_acceptor.bind(endpoint, error_code);

        if(error_code)
        {
            LOG(error, "socket", "ConnectionListener",
                "Failed to bind acceptor due to error: "
                + error_code.message());
            initialization_error = true;
            return;
        }

        // Start listening for connections
        //
        socket_acceptor.listen(
            boost::asio::socket_base::max_listen_connections, error_code);

        if(error_code)
        {
            LOG(error, "socket", "ConnectionListener",
                "Failed to start acceptor listen due to error: "
                + error_code.message());
            initialization_error = true;
            return;
        }
    }

    // ----------------------------------------------------------------------
    bool ConnectionListener::start(void)
    {
        if (initialization_error)
        {
            return false;
        }
        else
        {
            if (not socket_acceptor.is_open())
            {
                LOG(error, "socket", "start",
                    "Acceptor is not open; unable to accept new connections.");
                initialization_error = true;
            }
            else
            {
                do_accept();
            }
        }

        return not initialization_error;
    }

    // ----------   ------------------------------------------------------------
    void ConnectionListener::do_accept(void)
    {
        PlainRawSocketPtr new_connection(new PlainRawSocketConnection(
                driver_ptr,
                io_context));

        socket_acceptor.async_accept(
            new_connection->get_socket(),
            boost::bind(
                &ConnectionListener::on_accept,
                shared_from_this(),
                new_connection,
                boost::asio::placeholders::error));
    }

    // ----------------------------------------------------------------------
    void ConnectionListener::on_accept(
        PlainRawSocketPtr connection,
        boost::system::error_code error_code)
    {
        if (error_code)
        {
            LOG(error, "socket", "on_accept",
                "Could not accept connection due to error: "
                + error_code.message());
        }
        else
        {
            std::string source = "UNKNOWN";

            try
            {
                source = connection->get_socket().remote_endpoint().
                    address().to_string();
            }
            catch (boost::system::system_error &ex)
            {
                LOG(error, "socket", "on_accept",
                    "Failed to get remote IP address due to exception: "
                    + std::string(ex.what()));
            }
            catch (...)
            {
                LOG(error, "socket", "on_accept",
                    "Failed to get remote IP address due to unknown exception");
            }

            connection->start();
            new SocketClientConnection(driver_ptr, connection, source);
        }

        // Accept the next connection request.
        do_accept();
    }
}
}