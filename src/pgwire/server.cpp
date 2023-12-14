#include <pgwire/protocol.hpp>
#include <pgwire/server.hpp>

#include <memory>
#include <unordered_map>

#include <asio.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <endian/network.hpp>

using namespace asio;

namespace pgwire {
Session::Session(ip::tcp::socket &&socket)
    : _socket{std::move(socket)}, _startup_done(false), _running(false){

                                                        };
Session::~Session() = default;

void Session::start(ParseHandler &&handler) {
    _running = true;
    std::unordered_map<std::string, std::string> server_status = {
        {"server_version", "14"},     {"server_encoding", "UTF-8"},
        {"client_encoding", "UTF-8"}, {"DateStyle", "ISO"},
        {"TimeZone", "UTC"},
    };

    for (; _running;) {
        auto msg = this->read();

        if (msg == nullptr) {
            continue;
        }
        assert(msg != nullptr);

        switch (msg->type()) {
        case FrontendType::Invalid:
        case FrontendType::Startup:
            this->write(encode_bytes(AuthenticationOk{}));

            for (const auto &[k, v] : server_status) {
                this->write(encode_bytes(ParameterStatus{k, v}));
            }

            this->write(encode_bytes(ReadyForQuery{}));
            break;
        case FrontendType::SSLRequest:
            this->write(encode_bytes(SSLResponse{}));
            break;
        case FrontendType::Query: {
            auto *query = static_cast<Query *>(msg.get());

            auto prepared = handler(query->query);
            this->write(encode_bytes(RowDescription{prepared.fields}));

            Writer writer{prepared.fields.size()};
            prepared.handler(writer, {});
            this->write(encode_bytes(writer));

            this->write(encode_bytes(
                CommandComplete{std::format("SELECT {}", writer.num_rows())}));

            this->write(encode_bytes(ReadyForQuery{}));

            break;
        }
        case FrontendType::Terminate:
            _running = false;
            break;
        case FrontendType::Bind:
        case FrontendType::Close:
        case FrontendType::CopyFail:
        case FrontendType::Describe:
        case FrontendType::Execute:
        case FrontendType::Flush:
        case FrontendType::FunctionCall:
        case FrontendType::Parse:
        case FrontendType::Sync:
        case FrontendType::GSSResponse:
        case FrontendType::SASLResponse:
        case FrontendType::SASLInitialResponse:
            // std::cout << "message type still not handled, type="
            //           << int(msg->type()) << "tag=" << char(msg->tag())
            //           << std::endl;
            break;
        }
    }
}

static std::unordered_map<FrontendTag, std::function<FrontendMessage *()>>
    sFrontendMessageRegsitry = {
        {FrontendTag::Query, []() { return new Query; }},
        {FrontendTag::Terminate, []() { return new Terminate; }},
};

FrontendMessagePtr Session::read() {
    // std::cout << "reading startup=" << _startup_done << std::endl;
    if (!_startup_done) {
        return read_startup();
    }

    constexpr auto kHeaderSize = sizeof(MessageTag) + sizeof(int32_t);
    Bytes header(kHeaderSize);
    MessageTag tag = 0;
    int32_t len = 0;

    asio::read(_socket, buffer(header), asio::transfer_exactly(kHeaderSize));

    Buffer headerBuffer(std::move(header));
    tag = headerBuffer.get_numeric<MessageTag>();
    len = headerBuffer.get_numeric<int32_t>();
    len = len - sizeof(int32_t); // to exclude it self length

    Bytes body(len);
    asio::read(_socket, buffer(body), asio::transfer_exactly(body.size()));

    auto it = sFrontendMessageRegsitry.find(FrontendTag(tag));
    if (it == sFrontendMessageRegsitry.end()) {
        // std::cout << "message tag '" << tag << "' not supported, len=" << len
        //           << std::endl;
        return nullptr;
    }

    Buffer buff(std::move(body));
    auto fn = it->second;
    auto message = FrontendMessagePtr(fn());
    message->decode(buff);

    return message;
}
FrontendMessagePtr Session::read_startup() {
    int32_t len = 0;
    int32_t lenBuf = 0;
    Bytes bytes = {};

    asio::read(_socket,
               buffer(reinterpret_cast<uint8_t *>(&lenBuf), sizeof(int32_t)),
               asio::transfer_exactly(sizeof(int32_t)));
    len = endian::network::get<int32_t>(reinterpret_cast<uint8_t *>(&lenBuf));

    std::size_t size = len - sizeof(int32_t);
    bytes.resize(size);
    asio::read(_socket, buffer(bytes), asio::transfer_exactly(size));

    Buffer buf{std::move(bytes)};
    auto msg = std::make_unique<StartupMessage>();
    msg->decode(buf);

    if (!msg->is_ssl_request)
        _startup_done = true;

    return msg;
}
void Session::write(Bytes &&b) { asio::write(_socket, buffer(b)); }

Server::Server(io_context &io_context, ip::tcp::endpoint endpoint,
               Handler &&handler)
    : _io_context{io_context}, _acceptor{io_context, endpoint},
      _handler(std::move(handler)){};

void Server::start() {
    for (;;) {
        accept();
    }
}

void Server::accept() {
    ip::tcp::socket socket(_io_context);
    _acceptor.accept(socket);

    std::thread([s = std::move(socket), this]() mutable {
        Session session(std::move(s));
        auto handler = _handler(session);
        session.start(std::move(handler));
    }).detach();
}

} // namespace pgwire