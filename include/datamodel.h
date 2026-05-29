#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <semaphore>
#include <sizeutils.hpp>
#include <stop_token>
#include <system_error>
#include <toy_concurrency/concurrency_utils.h>
#include <utils/logger_utils.h>
#include <vector>

namespace data {

namespace details {

inline size_t getUID() {
  static std::atomic<size_t> id{0};
  return id.fetch_add((size_t)1, std::memory_order::relaxed);
}

inline auto &&get_logger() {
  static const auto path = utils::get_app_log_dir("LearnQt") / "datamodel.log";
  static utils::logger_t logger{"datamodel logger", path.string(), 1048576 * 10};
  return logger;
}

} // namespace details


namespace fs = std::filesystem;

inline std::expected<fs::file_status, std::error_code>
getStatusOf(const fs::path &path) {
  std::error_code ec;
  auto status = fs::status(path, ec);
  if (ec != std::error_code {}) {
    return std::unexpected{ec};
  }
  return status;
}

struct DirView {
  using Path = fs::path;
  using fsize = utils::fsize;
  enum class Status : std::uint8_t { Done, Searching, NotInit, Error };
  enum class ChildrenIterStatus : std::uint8_t { NotStable, Stable };
  struct Err {};

  struct MetaInfo {
    Status status = Status::NotInit;
    ChildrenIterStatus ch_iter_status = DirView::ChildrenIterStatus::NotStable;
    bool is_dir = false;
    fsize volume{0};
  };

  size_t uid = details::getUID();
  Path root;
  MetaInfo info;
  std::vector<DirView> children;
  std::unique_ptr<std::binary_semaphore> available =
      std::make_unique<std::binary_semaphore>(1);

  struct lock_guard_t {
    std::binary_semaphore *flag = nullptr;
    ~lock_guard_t() {
      _release(flag);
    }
    lock_guard_t(const lock_guard_t &other) = delete;
    lock_guard_t &operator=(const lock_guard_t &other) = delete;
    lock_guard_t(lock_guard_t &&other) noexcept : flag{other.flag} {
      other.flag = nullptr;
    }
    lock_guard_t &operator=(lock_guard_t &&other) noexcept {
      _release(flag);
      flag = other.flag;
      other.flag = nullptr;
      return *this;
    }
    lock_guard_t(std::binary_semaphore &flag) : flag{std::addressof(flag)} {}

    static void _release(std::binary_semaphore *&ptr) {
      if (ptr != nullptr) {
        ptr->release(1);
        ptr = nullptr;
      }
    }
  };

  lock_guard_t lock() const {
    available->acquire();
    return lock_guard_t{*available};
  }
  
  /**
   * @warning 内部会 acquire lock
   */
  std::expected<fsize, Err> getVolume() const {
    auto _lock = lock();
    if (info.status == Status::NotInit || info.status == Status::Error) {
      return std::unexpected{Err{}};
    }
    if (info.status == Status::Done) {
      return info.volume;
    }
    if (info.is_dir) {
      return std::ranges::fold_left(children, fsize{0}, [](fsize v, const DirView &ch) {
        return v + ch.getVolume().value_or(fsize{0});
      });
    }
    return fsize{0};
  }

  /**
   * @warning 内部会 acquire lock
   */
  Status getStatus() const {
    auto lc = lock();
    return info.status;
  }

  bool isChildrenStable() const {
    if (info.is_dir) {
      auto lc = lock();
      return info.ch_iter_status == ChildrenIterStatus::Stable;
    }
    return true;
  }

  DirView() = delete;
  
  DirView(const Path &path) : root{path}, 
    info{
      .is_dir = getStatusOf(path)
        .transform([](const fs::file_status &s) {
          return s.type() == fs::file_type::directory;
        })
        .value_or(false)
    } {}
  
  DirView(const Path &path, bool is_dir) : root{path}, info{.is_dir = is_dir} {}

  struct SnapshotInfo {
    size_t uid;
    MetaInfo info;
    Path path;
  };

  struct Snapshot {
    DirView &dvref; // NOLINT
    size_t uid;
    MetaInfo info;
    Path path;
    std::vector<SnapshotInfo> children;
  };

  /**
   * @warning 内部会 acquire lock
   */
  Snapshot getSnapshots() {
    Snapshot snapshot{.dvref = *this, .uid = uid, .path = root};
    snapshot.info = {
      .status = getStatus(),
      .is_dir = info.is_dir,
      .volume = getVolume().value_or(fsize{0})
    };

    auto lc = lock();
    snapshot.children.reserve(children.size());
    for (const auto &ch : children) {
      SnapshotInfo sinfo{.uid = ch.uid, .path = ch.root};
      sinfo.info = {
        .status = ch.getStatus(),
        .is_dir = ch.info.is_dir,
        .volume = ch.getVolume().value_or(fsize{0})
      };
      snapshot.children.emplace_back(std::move(sinfo));
    }
    return snapshot;
  }
};

namespace details {

inline void processRegularFile(DirView &file, const fs::directory_entry &entry) {
  std::error_code ec;
  const auto size = entry.file_size(ec);
  auto lock = file.lock();
  if (ec != std::error_code{}) {
    get_logger()->error("fail to get size of {}, msg: {}", entry.path().string(), ec.message());
    file.info.status = DirView::Status::Error;
    return;
  }
  file.info.volume = DirView::fsize{static_cast<DirView::fsize::value_t>(size)};
  file.info.status = DirView::Status::Done;
}

} // namespace details

/**
 * @warning callback 中不可读取 dir 及其 child (recursively) 的内容; 否则可能死锁
 */
template <std::invocable F>
inline void searchDir(DirView &dir, F &&callback, std::stop_token abort) { // NOLINT
  if (abort.stop_requested()) {
    return;
  }
  if (dir.info.is_dir) {
    {
      auto lock = dir.lock();
      dir.info.status = DirView::Status::Searching;
      dir.info.ch_iter_status = DirView::ChildrenIterStatus::NotStable;
    }
    try {
      for (const auto &entry : fs::directory_iterator{
          dir.root, fs::directory_options::skip_permission_denied
        }) {
        if (abort.stop_requested()) {
          return;
        }

        std::error_code ec;
        const auto status = entry.symlink_status(ec);
        if (ec != std::error_code{}) {
          details::get_logger()->error("fail to get status of {}, msg: {}",
            entry.path().string(), ec.message());
          continue;
        }
        
        if (status.type() == fs::file_type::directory) {
          auto lock = dir.lock();
          dir.children.emplace_back(entry.path(), true);
        } 
        
        else if (status.type() == fs::file_type::regular) {
          auto &ch = [&]() -> DirView & {
            auto lock = dir.lock();
            return dir.children.emplace_back(entry.path(), false);
          }();
          details::processRegularFile(ch, entry);
          std::invoke(std::forward<F>(callback));
        }
      }
    } catch (const fs::filesystem_error &e) {
      details::get_logger()->error("fail to searchDir {}, msg: {}, what: {}",
        dir.root.string(), e.code().message(), e.what());
      {
        auto lock = dir.lock();
        dir.info.status = DirView::Status::Error;
      }
      std::invoke(std::forward<F>(callback));
      return;
    } catch (...) {
      details::get_logger()->error("fail to searchDir {}", dir.root.string());
      {
        auto lock = dir.lock();
        dir.info.status = DirView::Status::Error;
      }
      std::invoke(std::forward<F>(callback));
      return;
    }
    {
      auto lc = dir.lock();
      dir.info.ch_iter_status = DirView::ChildrenIterStatus::Stable;
    }
    // 这里不持有 dir 的锁; 自此 dir.children 不发生变化, 迭代器不会失效, 引用也都有效
    for (auto &ch : dir.children) {
      if (ch.info.is_dir) {
        searchDir(ch, std::forward<F>(callback), abort);
      }
    }
    const auto volume = dir.getVolume().value_or(DirView::fsize{0});
    {
      auto lock = dir.lock();
      dir.info.volume = volume;
      dir.info.status = DirView::Status::Done;
    }
  }
  std::invoke(std::forward<F>(callback));
}

} // namespace data
