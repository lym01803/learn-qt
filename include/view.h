#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QtCore/qabstractitemmodel.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qhash.h>
#include <QtCore/qnamespace.h>
#include <QtCore/qobject.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qstringview.h>
#include <QtCore/qtmetamacros.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <datamodel.h>
#include <memory>
#include <mutex>
#include <optional>
#include <qqmlintegration.h>
#include <stop_token>
#include <thread>
#include <toy_concurrency/async_tool.h>
#include <toy_concurrency/concurrency_utils.h>
#include <unordered_map>
#include <utility>


namespace demo {

struct SimpleData {
  template <std::invocable F> 
  void modify(F &&callback) {
    text = text + "A";
    std::invoke(std::forward<F>(callback));
  }
  std::string const &get() const {
    return text; 
  }

  std::string text;
};

class SimpleViewModel: public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("SimpleViewModel is QML_UNCREATABLE")

  Q_PROPERTY(QString text READ text NOTIFY textChanged)

public:
  QString text() const { 
    return {data.get().c_str()}; 
  }

  Q_INVOKABLE void onclick() {
    data.modify([this]() {
      emit textChanged();
    });
  }

  SimpleViewModel(SimpleData &data) : data{data} {}

signals:
  void textChanged();

private:
  SimpleData &data; // 生命周期足够长
};

} // namespace demo

namespace view_details {

utils::logger_t& get_logger();

} // namespace view_details

struct FreqUtils {
  std::string name = "Freq Stats";
  std::chrono::system_clock::time_point ts{std::chrono::system_clock::now()};
  std::chrono::system_clock::duration interval{std::chrono::microseconds{10}};
  int count = 0;
  static constexpr int logFreq = 10;

  template <typename C, typename D>
  void record(const std::chrono::time_point<C, D> &tp) {
    interval = tp - ts;
    ts = tp;
    ++count;
    if (count % logFreq == 0) {
      view_details::get_logger()->info("{}, Interval: {} ms", name,
        std::chrono::duration_cast<std::chrono::milliseconds>(interval).count());
    }
  }
};

struct QueryUtil { // NOLINT
  struct QInfo {
    using TimePoint = std::chrono::steady_clock::time_point;
    std::string text;
    TimePoint ts;
  };

  std::optional<QInfo> query;
  std::mutex mutex;
  std::condition_variable cv;
  std::stop_source stop;
  playground::runner<async::cancellable_function<void>> worker{4};

  ~QueryUtil() {
    stop.request_stop();
    cv.notify_all();
  }

  // 等待 confirmInterval 以确认没有新 query 产生
  std::string getQueryImpl(std::stop_token abort,
                           std::chrono::milliseconds confirmInterval) {
    auto qinfo = [this, abort]() -> QInfo {
      std::stop_callback callback{abort, [this]() {
        cv.notify_all();
      }};
      std::unique_lock lock{mutex};
      cv.wait(lock, [this, abort]() {
        return stop.stop_requested() || abort.stop_requested() || query;
      });
      if (stop.stop_requested() || abort.stop_requested()) {
        return {.text = "", .ts = QInfo::TimePoint{}};
      }
      return std::exchange(query, std::nullopt).value();
    }(); // lock released

    while (!stop.stop_requested() && !abort.stop_requested() && qinfo.text != "") {
      std::this_thread::sleep_until(qinfo.ts + confirmInterval);
      auto detect = [this]() -> std::optional<QInfo> {
        std::unique_lock lock{mutex};
        return std::exchange(query, std::nullopt);
      }();
      if (detect.has_value()) {
        qinfo = std::move(detect).value();
      } else {
        break;
      }
    }
    return qinfo.text;
  }

  async::co_task_with<std::string> getQuery(std::stop_token abort, 
      std::chrono::milliseconds confirmInterval = std::chrono::milliseconds{150}) {
    co_return co_await async::lift([this, abort, confirmInterval]() { 
      return this->getQueryImpl(abort, confirmInterval);
    }).on(worker);
  }

  void setQuery(std::string query) {
    std::unique_lock lock{mutex};
    this->query = QInfo{.text = std::move(query), .ts = std::chrono::steady_clock::now()};
    cv.notify_one();
  }
};

struct SortUtil {
  struct Projector { // NOLINT
    virtual ~Projector() = default;
    virtual std::int64_t operator()(const data::DirView::SnapshotInfo &info) = 0;
  };
  struct SizeProjector : public Projector {
    std::int64_t operator()(const data::DirView::SnapshotInfo &info) override;
  };
  void sort(data::DirView::Snapshot &snapshot);
  void reset() {
    currentProjector = std::make_unique<SizeProjector>();
  }

  std::unique_ptr<Projector> currentProjector = std::make_unique<SizeProjector>();
};

struct QueryProjector : public SortUtil::Projector {
  std::int64_t operator()(const data::DirView::SnapshotInfo &info) override;

  QueryProjector(const std::string &query) : query{query} {}

  std::int64_t calcScore(const std::string &str);

  std::string query;
  std::unordered_map<size_t, std::int64_t> scoreCache;
};

struct NavigUtil {
  static auto splitPath(const std::filesystem::path &root, std::filesystem::path path) {
    std::vector<std::filesystem::path> paths;
    while (path != root) {
      paths.emplace_back(path.filename());
      path = path.parent_path();
    }
    std::ranges::reverse(paths);
    return paths;
  }

  static data::DirView *getDirView(data::DirView &dv, const std::filesystem::path &path) {
    const auto paths = splitPath(dv.root, path);
    auto *dir = &dv;
    for (const auto &file : paths) {
      if (!(dir->isChildrenStable())) {
        return nullptr;
      }
      bool found = false;
      for (auto &ch : dir->children) {
        if (ch.root.filename() == file) {
          dir = &ch;
          found = true;
          break;
        }
      }
      if (!found) {
        return nullptr;
      }
    }
    return dir;
  }
};

class FileListView: public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("FileListView is QML_UNCREATABLE")

public:
  using SnapshotT = data::DirView::Snapshot;

  FileListView(std::unique_ptr<SnapshotT> &snapshot, QObject *parent = nullptr)
    : QAbstractListModel{parent}, snapshot{snapshot} {}

  enum class Role : int { Path = Qt::UserRole + 1, FileName, Size, Ratio, IsDir, Id }; // NOLINT

  QHash<int, QByteArray> roleNames() const override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  struct ResetGuard {
    ResetGuard(FileListView *flv, int prev, int curr) : 
        flv{flv}, prev{prev}, curr{curr} {
      if (prev < curr) {
        flv->beginInsertRows(QModelIndex{}, prev, curr - 1);
      } else if (prev > curr) {
        flv->beginRemoveRows(QModelIndex{}, curr, prev - 1);
      }
    }
    ~ResetGuard() {
      release();
    }
    ResetGuard(const ResetGuard &other) = delete;
    ResetGuard &operator=(const ResetGuard &other) = delete;
    ResetGuard(ResetGuard &&other) noexcept :
        flv{other.flv}, prev{other.prev}, curr{other.curr} {
      other.flv = nullptr;
    }
    ResetGuard &operator=(ResetGuard &&other) noexcept {
      release();
      flv = other.flv;
      other.flv = nullptr;
      prev = other.prev;
      curr = other.curr;
      return *this;
    }
  private:
    void release() {
      if (flv != nullptr) {
        if (prev < curr) {
          flv->endInsertRows();
        } else if (prev > curr) {
          flv->endRemoveRows();
        }
        if (curr > 0) {
          emit flv->dataChanged(flv->createIndex(0, 0),
                                flv->createIndex(curr - 1, 0));
        }
        flv = nullptr;
      }
    }

    FileListView *flv = nullptr;
    int prev;
    int curr;
  };

  // must call at GUI thread
  ResetGuard scopedReset(int prevRows, int currRows) { 
    return ResetGuard{this, prevRows, currRows};
  }

private:
  std::unique_ptr<SnapshotT> &snapshot;
};


class NavigListView: public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("NavigListView is QML_UNCREATABLE")

public:
  using SnapshotT = data::DirView::Snapshot;

  NavigListView(std::filesystem::path &root, std::unique_ptr<SnapshotT> &snapshot, 
    QObject *parent = nullptr) : root{root}, QAbstractListModel{parent}, snapshot{snapshot} {
    reCalcCache();
  }

  struct ItemT {
    QString path;
    int order;
  };

  enum class Role : int { Path = Qt::UserRole + 1, Order }; // NOLINT

  QHash<int, QByteArray> roleNames() const override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  struct ResetGuard {
    explicit ResetGuard(NavigListView *nlv) : nlv{nlv} {
      nlv->beginResetModel();
    }
    ~ResetGuard() { release(); }
    ResetGuard(const ResetGuard &other) = delete;
    ResetGuard &operator=(const ResetGuard &other) = delete;
    ResetGuard(ResetGuard &&other) noexcept : nlv{other.nlv} {
      other.nlv = nullptr;
    }
    ResetGuard &operator=(ResetGuard &&other) noexcept {
      release();
      nlv = other.nlv;
      other.nlv = nullptr;
      return *this;
    }
  private:
    void release() {
      if (nlv != nullptr) {
        nlv->reCalcCache();
        nlv->endResetModel();
        nlv = nullptr;
      }
    }
    NavigListView *nlv = nullptr;
  };

  void reCalcCache();

  ResetGuard scopedReset() {
    return ResetGuard{this};
  }
private:
  std::filesystem::path &root;
  std::unique_ptr<SnapshotT> &snapshot;
  std::vector<ItemT> cache;
};


class DirViewModel: public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("DirViewModel is QML_UNCREATABLE")

  Q_PROPERTY(QString path READ getRoot NOTIFY nodeChanged);
  Q_PROPERTY(QString size READ getSize NOTIFY update);
  Q_PROPERTY(long long numFiles READ getNumFiles NOTIFY update);
  Q_PROPERTY(NavigListView * navigList READ getNavigListView CONSTANT);
  Q_PROPERTY(FileListView * fileList READ getFileListView CONSTANT);
  Q_PROPERTY(bool isRoot READ isRoot NOTIFY nodeChanged);
  Q_PROPERTY(bool isDir READ isDir NOTIFY nodeChanged);
public:
  using SnapshotT = data::DirView::Snapshot;

  DirViewModel(data::DirView dirView, QObject *parent = nullptr);
  
  DirViewModel() = delete;
  DirViewModel(const DirViewModel &other) = delete;
  DirViewModel(DirViewModel &&other) = delete; // because of QObject / atomic member
  DirViewModel &operator=(const DirViewModel &other) = delete;
  DirViewModel &operator=(DirViewModel &&other) = delete; // because of QObject / atomic member

  ~DirViewModel();

  QString getRoot() const {
    const auto path = snapshot->path;
    return QString::fromStdString(path.string());
  }

  QString getSize() const {
    const auto size = snapshot->info.volume;
    return QString::fromStdString(std::format("{:2A}", size));
  }

  long long getNumFiles() const {
    return static_cast<long long>(snapshot->children.size());
  }

  FileListView *getFileListView() {
    view_details::get_logger()->info("&flv: {}", (size_t)&flv);
    return &flv;
  }

  NavigListView *getNavigListView() {
    view_details::get_logger()->info("&nlv: {}", (size_t)&nlv);
    return &nlv;
  }

  bool isRoot() const {
    return snapshot->path == dv.root;
  }

  bool isDir() const {
    return snapshot->dvref.info.is_dir;
  }

  // called by qml, in GUI thread
  Q_INVOKABLE void gotoChild(size_t id);

  // called by qml, in GUI thread
  Q_INVOKABLE void gotoAncestor(int order);

  Q_INVOKABLE void openDir();

  Q_INVOKABLE void setQuery(const QString &str) {
    auto q = str.toStdString();
    view_details::get_logger()->info("Search Query: {}", q);
    query.setQuery(std::move(q));
  }

signals:
  void update();
  void nodeChanged();
  void clearQuery();

private:
  data::DirView dv;
  /**
   * @warning Actor: runner
   */
  data::DirView *dv_ptr = &dv;
  /**
   * @warning Actor: runner
   */
  SortUtil sorter;
  /**
   * @warning Actor: Qt GUI thread
   */
  std::unique_ptr<SnapshotT> snapshot;
  NavigListView nlv;
  FileListView flv;
  
  std::atomic<bool> updateSignal{false};
  /**
   * @warning Actor: runner
   */
  FreqUtils freq{.name = "DirViewModel UI Freq"};
  playground::runner<async::cancellable_function<void>> runner;
  
  std::stop_source stop;
  async::task_future<void> eventPollFuture;
  std::thread searchRunner;

  QueryUtil query;
  async::task_future<void> queryFuture;

  auto eventPoll(std::stop_token token) -> async::co_task;

  /**
   * @warning Actor: runner
   */
  SnapshotT getNewSnapshot();

  /**
   * @warning Actor: runner
   */
  void refreshSnapshot();

  /**
   * @warning Actor: Qt GUI thread
   */
  void updateSnapshot(std::unique_ptr<SnapshotT> newSnapshot);

  /**
   * @warning Actor: runner
   */
  void setDirViewPtr(data::DirView *newDvPtr);

  auto processQuery(std::stop_token token) -> async::co_task;

  auto calcQueryScore(std::stop_token token, std::string q) -> async::co_task;
};


class MainEntrance: public QObject {
  Q_OBJECT
  QML_ELEMENT

public:
  enum class State : int { // NOLINT
    Init,
    SelectedPath
  };
  Q_ENUM(State);

  Q_PROPERTY(State state READ state NOTIFY stateChanged);
  Q_PROPERTY(DirViewModel * dirViewModel READ dirViewModel NOTIFY stateChanged);

  Q_INVOKABLE void setPath(const QString &qpath) {
    // const auto path = data::fs::path{"/Users/lym01803"};
    const QUrl qurl = QUrl(qpath);
    const QString localPath = qurl.toLocalFile();
    auto path = [&]() {
#if defined (_WIN32)
      return std::filesystem::path{localPath.toStdWString()};
#else 
      return std::filesystem::path{localPath.toStdString()};
#endif
    }();
    if (!path.has_filename()) {
      path = path.parent_path();
    }
    view_details::get_logger()->info("MainEntrance, search path: {}", path.string());
    setDVMPath(path);
  }

  State state() const {
    return entranceState;
  }

  void setState(State state) {
    entranceState = state;
    emit stateChanged();
  }

  DirViewModel *dirViewModel() {
    return dvm.get();
  }

signals:
  void stateChanged();

private:
  State entranceState{State::Init};
  std::unique_ptr<DirViewModel> dvm;

  void setDVMPath(const std::filesystem::path &path) {
    dvm = std::make_unique<DirViewModel>(data::DirView{path}, nullptr);
    QQmlEngine::setObjectOwnership(dvm.get(), QQmlEngine::CppOwnership);
    setState(State::SelectedPath);
  }
};

