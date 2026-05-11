#include "AmqpSession.hpp"
#include <proton/sender_options.hpp>
#include <proton/receiver_options.hpp>
#include <proton/source_options.hpp>
#include <proton/target_options.hpp>
#include <proton/connection_options.hpp>
#include <proton/transport.hpp>
#include <proton/symbol.hpp>
#include <chrono>

using namespace std::chrono;
using json = nlohmann::json;

AmqpSession::AmqpSession() : container_(*this) {}

AmqpSession::~AmqpSession() { stop(); }

void AmqpSession::start(const std::string& url,  const std::string& user,
                         const std::string& pass, const std::string& req_q,
                         const std::string& resp_q)
{
    url_ = url; user_ = user; pass_ = pass; req_q_ = req_q; resp_q_ = resp_q;
    loop_thread_ = std::thread([this]{ container_.run(); });
    // Wait until connected (on_sendable sets connected_)
    std::unique_lock<std::mutex> lk(conn_mu_);
    conn_cv_.wait_for(lk, seconds(10), [this]{ return connected_.load(); });
}

void AmqpSession::stop() {
    if (loop_thread_.joinable()) {
        container_.stop();
        loop_thread_.join();
    }
}

// ── proton callbacks ──────────────────────────────────────────────────────────

void AmqpSession::on_container_start(proton::container& c) {
    proton::connection_options copts;
    copts.user(user_).password(pass_).sasl_enabled(true).sasl_allow_insecure_mechs(true);

    conn_ = c.connect(url_, copts);

    // Request sender with ANYCAST capability (Artemis queue routing)
    proton::sender_options sopts;
    sopts.target(proton::target_options()
                     .capabilities({proton::symbol("queue")}));
    sender_ = conn_.open_sender(req_q_, sopts);

    // Response receiver with ANYCAST capability
    proton::receiver_options ropts;
    ropts.source(proton::source_options()
                     .capabilities({proton::symbol("queue")}));
    receiver_ = conn_.open_receiver(resp_q_, ropts);
}

void AmqpSession::on_sendable(proton::sender&) {
    sender_ready_ = true;
    flush();
    if (!connected_.exchange(true)) {
        std::lock_guard<std::mutex> lk(conn_mu_);
        conn_cv_.notify_all();
    }
}

void AmqpSession::flush() {
    std::lock_guard<std::mutex> g(out_mu_);
    while (!out_q_.empty() && sender_.credit() > 0) {
        proton::message m;
        m.body(out_q_.front());
        m.content_type("application/json");
        sender_.send(m);
        out_q_.pop_front();
    }
}

void AmqpSession::on_message(proton::delivery&, proton::message& msg) {
    std::string body;
    try {
        auto v = msg.body();
        if (v.type() == proton::STRING)       body = proton::get<std::string>(v);
        else if (v.type() == proton::BINARY)  body = std::string(proton::get<proton::binary>(v).begin(),
                                                                  proton::get<proton::binary>(v).end());
        else return;
        auto j   = json::parse(body);
        auto rid = j.value("request_id", std::string{});
        std::lock_guard<std::mutex> g(waiters_mu_);
        auto it = waiters_.find(rid);
        if (it != waiters_.end()) {
            std::lock_guard<std::mutex> wg(it->second->mu);
            it->second->body = body;
            it->second->done = true;
            it->second->cv.notify_one();
        }
    } catch (...) {}
}

void AmqpSession::on_connection_error(proton::connection& c) {
    connected_ = false;
}
void AmqpSession::on_transport_error(proton::transport&) {
    connected_ = false;
}

// ── Public API ────────────────────────────────────────────────────────────────

void AmqpSession::fire(const nlohmann::json& req) {
    std::string body = req.dump();
    {
        std::lock_guard<std::mutex> g(out_mu_);
        out_q_.push_back(body);
    }
    // Schedule flush on the proton thread (thread-safe)
    conn_.work_queue().schedule(proton::duration(0), [this]{ flush(); });
}

json AmqpSession::rpc(const json& req, int timeout_ms, std::atomic<bool>* stop_flag) {
    std::string rid = req.value("request_id", std::string{});
    auto waiter = std::make_shared<Waiter>();
    {
        std::lock_guard<std::mutex> g(waiters_mu_);
        waiters_[rid] = waiter;
    }

    fire(req);

    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    {
        std::unique_lock<std::mutex> lk(waiter->mu);
        while (!waiter->done) {
            if (stop_flag && stop_flag->load()) break;
            if (waiter->cv.wait_until(lk, deadline) == std::cv_status::timeout) break;
        }
    }

    {
        std::lock_guard<std::mutex> g(waiters_mu_);
        waiters_.erase(rid);
    }

    if (!waiter->done) return {};
    try { return json::parse(waiter->body); } catch (...) { return {}; }
}
