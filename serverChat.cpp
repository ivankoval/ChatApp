#include<iostream>
#include<list>
#include<map>
#include<queue>
#include<cstdlib>

#include<log4cxx/logger.h>
#include <log4cxx/xml/domconfigurator.h>

#include<boost/asio.hpp>
#include<boost/thread.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<boost/asio/ssl.hpp>

using namespace std;

using namespace log4cxx;
using namespace log4cxx::xml;

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;

typedef ssl::stream<tcp::socket> ssl_socket;
typedef boost::shared_ptr<ssl_socket> socket_ptr;
typedef boost::shared_ptr<string> string_ptr;
typedef map<socket_ptr, string_ptr> clientMap;
typedef boost::shared_ptr<clientMap> clientMap_ptr;
typedef boost::shared_ptr< list<socket_ptr> > clientList_ptr;
typedef boost::shared_ptr< queue<clientMap_ptr> > messageQueue_ptr;

io_service service;
tcp::acceptor acceptor(service, tcp::endpoint(tcp::v4(), 8001));
boost::mutex mtx;
boost::system::error_code ec;
clientList_ptr clientList(new list<socket_ptr>);
messageQueue_ptr messageQueue(new queue<clientMap_ptr>);

const int bufSize = 1024;
enum sleepLen
{
    sml = 100,
    lon = 200
};

//loggers
LoggerPtr loggerMainFunction(Logger::getLogger("MainFunction"));
LoggerPtr loggerAcceptorLoopFunction(Logger::getLogger("AcceptorLoopFunction"));
LoggerPtr loggerRequestLoopFunction(Logger::getLogger("RequestLoopFunction"));
LoggerPtr loggerResponseLoopFunction(Logger::getLogger("ResponseLoopFunction"));
LoggerPtr loggerDisconnectClientFunction(Logger::getLogger("DisconnectClientFunction"));

// function prototypes
bool clientSentExit(string_ptr);
void disconnectClient(socket_ptr);
void acceptorLoop();
void requestLoop();
void responseLoop();

int main(int argc, char** argv)
{
    DOMConfigurator::configure("Log4cxxConfig.xml");

    LOG4CXX_INFO(loggerMainFunction, "Main function started!");

    thread_group threads;

    threads.create_thread(boost::bind(acceptorLoop));
    this_thread::sleep(posix_time::millisec(sleepLen::sml));

    threads.create_thread(boost::bind(requestLoop));
    this_thread::sleep( posix_time::millisec(sleepLen::sml));

    threads.create_thread(boost::bind(responseLoop));
    this_thread::sleep( posix_time::millisec(sleepLen::sml));

    threads.join_all();

    puts("Press any key to continue...");
    getc(stdin);
    return EXIT_SUCCESS;
}

void acceptorLoop()
{
//    ssl context
    ssl::context context_(service, ssl::context::sslv23);

    context_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::single_dh_use);
    context_.use_certificate_chain_file("certs/server.crt");
    context_.use_private_key_file("certs/server.key", ssl::context::pem);
    context_.use_tmp_dh_file("certs/dh512.pem");

    LOG4CXX_INFO(loggerAcceptorLoopFunction, "Waiting for clients...");

    try {
        for(;;)
        {
            socket_ptr clientSock(new ssl_socket(service, context_));

            acceptor.accept(clientSock->lowest_layer());

            clientSock->handshake(ssl::stream_base::server, ec);

            if(!ec)
            {
                LOG4CXX_INFO(loggerAcceptorLoopFunction, "New client joined!");

                mtx.lock();
                clientList->emplace_back(clientSock);
                mtx.unlock();

                LOG4CXX_INFO(loggerAcceptorLoopFunction, clientList->size() << " total clients");

            } else {
                throw boost::system::system_error(ec);
            }
        }

    }
    catch (std::exception& e) {
        LOG4CXX_ERROR(loggerAcceptorLoopFunction, e.what());
    }
}

void requestLoop()
{
    for(;;)
    {
        if(!clientList->empty())
        {

            mtx.lock();
            for(auto& clientSock : *clientList)
            {
                if(clientSock->lowest_layer().available())
                {
                    char readBuf[bufSize] = {0};

                    int bytesRead = clientSock->read_some(buffer(readBuf, bufSize));

                    string_ptr msg(new string(readBuf, bytesRead));

                    if(clientSentExit(msg))
                    {
                        disconnectClient(clientSock);
                        break;
                    }

                    clientMap_ptr cm(new clientMap);
                    cm->insert(pair<socket_ptr, string_ptr>(clientSock, msg));

                    messageQueue->push(cm);

                    LOG4CXX_INFO(loggerRequestLoopFunction, "Message by " << *msg);
                }
            }
            mtx.unlock();

        }
        this_thread::sleep( posix_time::millisec(sleepLen::lon));
    }
}

bool clientSentExit(string_ptr message)
{
    if(message->find("exit") != string::npos)
        return true;
    else
        return false;
}

void disconnectClient(socket_ptr clientSock)
{
    boost::system::error_code error_code;

    auto position = find(clientList->begin(), clientList->end(), clientSock);

    clientSock->lowest_layer().shutdown(tcp::socket::shutdown_both, error_code);

    clientSock->lowest_layer().close();
    clientList->erase(position);

    LOG4CXX_INFO(loggerDisconnectClientFunction, "Client Disconnected!  " << clientList->size() << " total clients");

}

void responseLoop()
{
    for(;;)
    {
        if(!messageQueue->empty())
        {
            auto message = messageQueue->front();

            mtx.lock();
            for(auto& clientSock : *clientList)
            {
                clientSock->write_some(buffer(*(message->begin()->second), bufSize));
            }
            mtx.unlock();

            mtx.lock();
            messageQueue->pop();
            mtx.unlock();
        }

        this_thread::sleep( posix_time::millisec(sleepLen::lon));
    }
}