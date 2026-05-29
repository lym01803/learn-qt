#pragma once

#include <filesystem>
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

inline std::filesystem::path get_app_log_dir(const std::string &app_name) {
  auto log_dir = [&]() -> std::filesystem::path {
#ifndef NDEBUG
    return std::filesystem::current_path() / "log";
#else
  #if defined (_WIN32)
    const char *app_data = std::getenv("APPDATA");
    if (app_data) {
      return std::filesystem::path(std::string{app_data}) / app_name / "log";
    }
  #elif defined (__APPLE__)
    const char *home = std::getenv("HOME");
    if (home) {
      return std::filesystem::path(std::string{home}) / "Library" 
              / "Application Support" / app_name / "log";
    }
  #endif
    return std::filesystem::current_path() / "log";
#endif
  }();
  if (!std::filesystem::exists(log_dir)) {
    std::filesystem::create_directories(log_dir);
  }
  return log_dir;
}

}
