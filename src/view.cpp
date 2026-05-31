#include "toy_concurrency/async_tool.h"
#include "utf8utils.hpp"
#include "utils/logger_utils.h"
#include <QDesktopServices>
#include <QUrl>
#include <QtCore/qcontainerfwd.h>
#include <QtCore/qnamespace.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qstringview.h>
#include <QtCore/qurl.h>
#include <QtCore/qvariant.h>
#include <QtQml/qqmlengine.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <datamodel.h>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <view.h>

namespace view_details {
  
utils::logger_t& get_logger() {
  static const auto path = utils::get_app_log_dir("LearnQt") / "viewmodel.log";
  static utils::logger_t logger{
    "view model logger", path.string(), 1048576 * 10}; // NOLINT
  return logger;
}

} // namespace view_detail;

namespace  {

template <typename R1, typename P1, typename R2, typename P2>
void adjustInterval(const std::chrono::duration<R1, P1> &referenceInterval, 
  std::chrono::duration<R2, P2>& intervalToAdjust) {
  if (referenceInterval > intervalToAdjust * 2) {
    intervalToAdjust *= 2;
  } else if (referenceInterval * 2 < intervalToAdjust * 3 
      && intervalToAdjust > std::chrono::milliseconds{10}) {
    intervalToAdjust /= 2;
  }
}

} // namespace

DirViewModel::DirViewModel(data::DirView dirView, QObject *parent)
    : QObject{parent}, dv{std::move(dirView)},
      snapshot{std::make_unique<SnapshotT>(getNewSnapshot())},
      nlv{dv.root, snapshot, nullptr},
      flv{snapshot, nullptr},
      eventPollFuture{eventPoll(stop.get_token()).get_future()},
      queryFuture{processQuery(stop.get_token()).get_future()} {
  QQmlEngine::setObjectOwnership(&flv, QQmlEngine::CppOwnership);
  runner([this]() { this->searchTask(); });
}

void DirViewModel::gotoChild(size_t id) {
  auto lock = snapshot->dvref.lock();
  if (snapshot->dvref.info.ch_iter_status ==
      data::DirView::ChildrenIterStatus::Stable) {
    for (auto &ch : snapshot->dvref.children) {
      if (ch.uid == id) {
        auto *new_dv_ptr = &ch;
        setDirViewPtr(new_dv_ptr);
        break;
      }
    }
  }
}

void DirViewModel::gotoAncestor(int order) {
  auto path = snapshot->dvref.root;
  if (path == dv.root) {
    return;
  }
  for (int i = 0; i < order; ++i) {
    path = path.parent_path();
  }
  auto *new_dv_ptr = NavigUtil::getDirView(dv, path);
  if (new_dv_ptr != nullptr) {
    setDirViewPtr(new_dv_ptr);
  }
}

void DirViewModel::openDir() {
  const auto &path = snapshot->dvref.root;
  if (!std::filesystem::exists(path) && !snapshot->dvref.info.is_dir) {
    return;
  }
  const auto qpath = [&]() {
#if defined (_WIN32) 
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
  }();
  const auto qurl = QUrl::fromLocalFile(qpath);
  runner([qurl = std::move(qurl)]() {
    QDesktopServices::openUrl(qurl);
  });
}

auto DirViewModel::eventPoll(std::stop_token token) -> async::co_task { // NOLINT
  playground::runner<async::cancellable_function<void>> sleepRunner{0};
  auto sleepTime = std::chrono::milliseconds{10};

  auto sleep = [&](std::chrono::milliseconds ms) {
    return async::lift([&sleepRunner, ms]() {
      std::this_thread::sleep_for(ms);
    }).on(sleepRunner);
  };

  auto processEvent = async::lift([&, this]() {
    if (this->updateSignal.exchange(false, std::memory_order::acquire)) {
      this->refreshSnapshot();
    }
  }).on(this->runner);

  FreqUtils pollFreq{.name = "DirViewModel EventPoll Freq"};
  while (!token.stop_requested()) {
    co_await sleep(sleepTime);
    co_await processEvent;
    pollFreq.record(std::chrono::system_clock::now());
  }
}

void DirViewModel::searchTask() {
  bool expected = true;
  if (searchIdle.compare_exchange_strong(expected, false, 
      std::memory_order::acquire, std::memory_order::relaxed)) {
    emit searchIdleChange();
    auto *dir_view_ptr = dv_ptr;
    searchRunner([this, dir_view_ptr]() {
      data::searchDir(
        *dir_view_ptr, 
        [this]() {
          updateSignal.store(true, std::memory_order::release);
        }, 
        stop.get_token()
      );
      searchIdle.store(true, std::memory_order::release);
      emit searchIdleChange();
    });
  }
}

DirViewModel::SnapshotT DirViewModel::getNewSnapshot() {
  auto ss = dv_ptr->getSnapshots();
  sorter.sort(ss);
  return ss;
}

void DirViewModel::refreshSnapshot() {
  auto ssptr = std::make_unique<SnapshotT>(getNewSnapshot());
  QMetaObject::invokeMethod(this, 
    [this, ssptr = std::move(ssptr)]() mutable {
      this->updateSnapshot(std::move(ssptr));
    }, 
    Qt::QueuedConnection
  );
}

void DirViewModel::updateSnapshot(std::unique_ptr<SnapshotT> newSnapshot) {
  {
    auto gd = flv.scopedReset(static_cast<int>(snapshot->children.size()),
                              static_cast<int>(newSnapshot->children.size()));
    snapshot = std::move(newSnapshot);
  }
  emit update();
  runner([this, ts = std::chrono::system_clock::now()]() {
    freq.record(ts); 
  });
}

void DirViewModel::setDirViewPtr(data::DirView *newDvPtr) {
  runner([this, newDvPtr]() {
    this->dv_ptr = newDvPtr;
    emit this->clearQuery();
    query.setQuery("");
    auto ssptr = std::make_unique<SnapshotT>(getNewSnapshot());
    QMetaObject::invokeMethod(
      this,
      [this, ssptr = std::move(ssptr)]() mutable {
        auto gd = nlv.scopedReset();
        this->updateSnapshot(std::move(ssptr));
      },
      Qt::QueuedConnection
    );
    emit this->nodeChanged();
  });
}

auto DirViewModel::processQuery(std::stop_token token) -> async::co_task {
  while (!token.stop_requested()) {
    auto q = co_await query.getQuery(token);
    co_await async::execute_by(runner);
    if (q == "") {
      view_details::get_logger()->info("sorter reset");
      sorter.reset();
      updateSignal.store(true, std::memory_order::release);
    } else {
      view_details::get_logger()->info("sorter set to query: {}", q);
      co_await calcQueryScore(token, std::move(q));
    }
  }
}

auto DirViewModel::calcQueryScore(std::stop_token token, std::string q) 
    -> async::co_task {
  // run at runner
  co_await async::execute_by(runner);
  auto ts = std::chrono::high_resolution_clock::now();
  auto children = getNewSnapshot().children; // move construct
  QueryProjector qproj{q};
  int i = 0;
  for (const auto &ch : children) {
    if (token.stop_requested()) {
      co_return;
    }
    const auto score = qproj.calcScore(ch.path.filename().string());
    qproj.scoreCache.insert({ch.uid, score});  
    ++i;
    if (i % 100 == 0) {
      auto t = std::chrono::high_resolution_clock::now();
      if (t - ts > std::chrono::milliseconds{5}) {
        co_await async::execute_by(runner);
        ts = std::chrono::high_resolution_clock::now();
      } else {
        ts = t;
      }
    }
  }
  sorter.currentProjector = std::make_unique<QueryProjector>(std::move(qproj));
  updateSignal.store(true, std::memory_order::release);
}

DirViewModel::~DirViewModel() {
  stop.request_stop();
  try {
    queryFuture.get();
    eventPollFuture.get();
  } catch(...) {
    view_details::get_logger()->error("Uncaught exception during ~DirViewModel().");
    std::terminate();
  }
}

std::int64_t SortUtil::SizeProjector::operator()(const data::DirView::SnapshotInfo &info) {
  return info.info.volume.value();
}

void SortUtil::sort(data::DirView::Snapshot &snapshot) {
  std::ranges::sort(snapshot.children, std::greater<std::int64_t>{}, 
    [this](const data::DirView::SnapshotInfo &child) {
      return (*currentProjector)(child);
    }
  );
}

std::int64_t QueryProjector::operator()(const data::DirView::SnapshotInfo &info) {
  auto [iter, noKeyFound] = scoreCache.try_emplace(info.uid);
  if (noKeyFound) {
    iter->second = calcScore(info.path.filename().string());
  }
  return iter->second;
}

namespace {

bool capital_match(std::string_view sv_l, std::string_view sv_r) {
  if (sv_l == sv_r) {
    return true;
  }
  if (sv_l.size() == 1 && sv_r.size() == 1) {
    return std::tolower(sv_l[0]) == std::tolower(sv_r[0]);
  }
  return false;
}

}

std::int64_t QueryProjector::calcScore(const std::string &str) {
  constexpr int len = 5;
  static std::array<std::int64_t, len> d_score = {-5, -4, -3, -2, -1};
  static std::int64_t max_penalty = -20;

  std::int64_t score = 0;
  auto iter = utils::utf8_view{str}.begin();
  auto end = utils::utf8_view{str}.end();
  int pos = 0;

  for (const auto s : utils::utf8_view{query}) {
    int dis = 0;
    std::int64_t subscore = 0;
    while (iter != end && !capital_match(s, *iter)) {
      if (dis < len) {
        subscore += d_score[dis]; // NOLINT
      }
      ++dis;
      ++iter;
      ++pos;
    }
    if (iter == end) {
      score += max_penalty;
      continue;
    }
    if (pos == 0) {
      subscore += 10;
    } else if (pos < 5) {
      subscore += 5 - pos;
    }
    score += subscore;
    ++iter;
  }
  return score;
}

QHash<int, QByteArray> FileListView::roleNames() const {
  QHash<int, QByteArray> map;
  map[(int)Role::Path] = "path";
  map[(int)Role::FileName] = "fileName";
  map[(int)Role::Size] = "size";
  map[(int)Role::Ratio] = "ratio";
  map[(int)Role::IsDir] = "isDir";
  map[(int)Role::Id] = "id";
  return map;
}

int FileListView::rowCount(const QModelIndex &parent) const {
  return static_cast<int>(snapshot->children.size());
}

QVariant FileListView::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant{};
  }
  const auto row = index.row();
  const auto &item = snapshot->children.at(row);
  switch (static_cast<Role>(role)) {
    case Role::Path: { 
      return QVariant::fromValue(QString::fromStdString(item.path.string())); 
    }
    case Role::FileName: {
      return QVariant::fromValue(QString::fromStdString(item.path.filename().string()));
    }
    case Role::Size: {
      return QVariant::fromValue(QString::fromStdString(
        std::format("{:2A}", item.info.volume)
      ));
    }
    case Role::Ratio: {
      if (snapshot->info.volume.value() == 0) {
        return QVariant::fromValue(1. / static_cast<double>(snapshot->children.size()));
      }
      return QVariant::fromValue(static_cast<double>(item.info.volume.value()) 
        / static_cast<double>(snapshot->info.volume.value()));
    }
    case Role::IsDir: {
      return QVariant::fromValue(item.info.is_dir); 
    }
    case Role::Id: {
      return QVariant::fromValue(item.uid);
    }
    default: { return QVariant{}; }
  }
}

QHash<int, QByteArray> NavigListView::roleNames() const {
  QHash<int, QByteArray> map;
  map[(int)Role::Path] = "path";
  map[(int)Role::Order] = "order";
  return map;
}

int NavigListView::rowCount(const QModelIndex &parent) const {
  return static_cast<int>(cache.size());
}

QVariant NavigListView::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant{};
  }
  const auto row = index.row();
  const auto &item = cache.at(row);
  switch (static_cast<Role>(role)) {
    case Role::Order: { return QVariant::fromValue(item.order); }
    case Role::Path: { return QVariant::fromValue(item.path); }
    default: { return QVariant{}; }
  }
}

void NavigListView::reCalcCache() {
  cache.clear();
  auto paths = NavigUtil::splitPath(root, snapshot->path);
  cache.reserve(paths.size() + 1);
  cache.emplace_back(
    QString::fromStdU16String(root.u16string()), static_cast<int>(paths.size()));
  for (int i = 0; i < paths.size(); i++) {
    cache.emplace_back(QString::fromStdU16String(paths[i].u16string()), 
                       static_cast<int>(paths.size() - 1 - i));
  }
}

