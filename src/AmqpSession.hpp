#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <nlohmann/json.hpp>
#include <proton/messaging_handler.hpp>
#include <proton/container.hpp>
#include <proton/connection.hpp>
#include <proton/sender.hpp>
#include <proton/receiver.hpp>
#include <proton/message.hpp>
#include <proton/work_queue.hpp>

class AmqpSession : public proton::messaging_handler {
public:
    AmqpSession();
    ~AmqpSession();

    // Connect and start the proton event loop in a background thread.
    void start(const std::string& url,
               const std::string& user,
               const std::string& pass,
               const std::string& req_q  = "sdr.task.request",
               const std::string& resp_q = "sdr.task.response");

    // Send req, wait for response matching req["request_id"]. Returns {} on timeout.
    // Thread-safe; may be called from any thread except the proton thread.
    nlohmann::json rpc(const nlohmann::json& req,
                       int                   timeout_ms  = 20000,
                       std::atomic<bool>*    stop_flag   = nullptr);

    // Send without waiting for a response.
    void fire(const nlohmann::json& req);

    void stop();

private:
    // ── proton callbacks (all on proton thread) ──────────────────────────────
    void on_container_start(proton::container&)               override;
    void on_sendable       (proton::sender&)                  override;
    void on_message        (proton::delivery&, proton::message&) override;
    void on_connection_error(proton::connection&)             override;
    void on_transport_error (proton::transport&)              override;

    void flush();   // must be called on proton thread

    // ── state ────────────────────────────────────────────────────────────────
    std::string url_, user_, pass_, req_q_, resp_q_;

    proton::container  container_;
    std::thread        loop_thread_;

    proton::connection conn_;
    proton::sender     sender_;
    proton::receiver   receiver_;
    bool               sender_ready_ = false;

    // Outgoing queue (any thread → proton thread via work_queue)
    std::mutex               out_mu_;
    std::deque<std::string>  out_q_;

    // RPC waiters
    struct Waiter {
        std::mutex              mu;
        std::condition_variable cv;
        std::string             body;
        bool                    done = false;
    };
    std::mutex                                              waiters_mu_;
    std::unordered_map<std::string, std::shared_ptr<Waiter>> waiters_;

    std::atomic<bool> connected_{false};
    std::mutex        conn_mu_;
    std::condition_variable conn_cv_;
};
