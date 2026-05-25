#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <utils/logger_utils.h>

namespace utils {

namespace {

void set_spdlog_flush_interval() {
  struct spdlog_flush_interval_t {
    spdlog_flush_interval_t() { spdlog::flush_every(std::chrono::seconds{5}); }
  };
  static const spdlog_flush_interval_t dummy{};
}

} // namespace

logger_t::logger_t(const std::string &name, const std::string &log_file_path,
                   std::size_t max_size, std::size_t max_files,
                   std::size_t thread_pool_cap, std::size_t num_thread)
    : tp{std::make_shared<spdlog::details::thread_pool>(thread_pool_cap,
                                                        num_thread)},
      sk{std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file_path, max_size, max_files)},
      log{std::make_shared<spdlog::async_logger>(
          name, sk, tp, spdlog::async_overflow_policy::overrun_oldest)} {
  // log->flush_on(spdlog::level::err);
  log->flush_on(spdlog::level::info);
  spdlog::register_logger(log);
  set_spdlog_flush_interval();
}

spdlog::async_logger const *logger_t::operator->() const noexcept {
  return log.get();
}

spdlog::async_logger *logger_t::operator->() noexcept { 
  return log.get(); 
}

} // namespace utils
