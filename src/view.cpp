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
#include <chrono>
#include <cstdint>
#include <datamodel.h>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <view.h>

namespace view_details {
  
utils::logger_t& get_logger() {
  static utils::logger_t logger{
    "view model logger", "log/viewmodel.log", 1048576 * 10}; // NOLINT
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
      searchRunner{[this]() {
        data::searchDir(
            dv,
            [this]() {
              updateSignal.store(true, std::memory_order::release);
            },
            stop.get_token());
      }} {
  QQmlEngine::setObjectOwnership(&flv, QQmlEngine::CppOwnership);
}

void DirViewModel::gotoChild(size_t id) {
  if (snapshot->dvref.isChildrenStable()) {
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
    return QString::fromStdString(path.wstring());
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
      auto ssptr = std::make_unique<SnapshotT>(getNewSnapshot());
      QMetaObject::invokeMethod(this, [this, ssptr = std::move(ssptr)]() mutable {
        this->updateSnapshot(std::move(ssptr));
      }, Qt::QueuedConnection);
    }
  }).on(this->runner);

  FreqUtils pollFreq{.name = "DirViewModel EventPoll Freq"};
  while (!token.stop_requested()) {
    co_await sleep(sleepTime);
    co_await processEvent;
    pollFreq.record(std::chrono::system_clock::now());
  }
}

DirViewModel::SnapshotT DirViewModel::getNewSnapshot() {
  auto ss = dv_ptr->getSnapshots();
  sorter.sort(ss);
  return ss;
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

DirViewModel::~DirViewModel() {
  stop.request_stop();
  try {
    if (searchRunner.joinable()) {
      searchRunner.join();
    }
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
    QString::fromStdString(root.string()), static_cast<int>(paths.size()));
  for (int i = 0; i < paths.size(); i++) {
    cache.emplace_back(QString::fromStdString(paths[i].string()), 
                       static_cast<int>(paths.size() - 1 - i));
  }
}

