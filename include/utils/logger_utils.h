#pragma once

#include <memory>
#include <spdlog/async_logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/details/thread_pool.h>

namespace utils {

struct logger_t {
  std::shared_ptr<spdlog::details::thread_pool> tp;
  std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> sk;
  std::shared_ptr<spdlog::async_logger> log;

  logger_t(const std::string &name, const std::string &log_file_path,
           std::size_t max_size = 1048576, std::size_t max_files = 3,
           std::size_t thread_pool_cap = 8192, std::size_t num_thread = 1);

  spdlog::async_logger const *operator->() const noexcept;
  spdlog::async_logger *operator->() noexcept;
};

}
