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
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <datamodel.h>
#include <memory>
#include <qqmlintegration.h>
#include <stop_token>
#include <thread>
#include <toy_concurrency/async_tool.h>
#include <toy_concurrency/concurrency_utils.h>
#include <toy_concurrency/toyqueue.h>

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

struct SortUtil {
  struct Projector { // NOLINT
    virtual ~Projector() = default;
    virtual std::int64_t operator()(const data::DirView::SnapshotInfo &info) = 0;
  };
  struct SizeProjector : public Projector {
    std::int64_t operator()(const data::DirView::SnapshotInfo &info) override;
  };
  void sort(data::DirView::Snapshot &snapshot);

  SizeProjector sizeProjector;
  Projector *currentProjector = &sizeProjector; // no ownership
};

class FileListView: public QAbstractListModel { // NOLINT
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("FileListView is QML_UNCREATABLE")

public:
  using SnapshotT = data::DirView::Snapshot;

  FileListView(std::unique_ptr<SnapshotT> &snapshot, QObject *parent = nullptr)
    : QAbstractListModel{parent}, snapshot{snapshot} {}

  enum class Role : int { Path = Qt::UserRole + 1, Size, Ratio, IsDir }; // NOLINT

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

class DirViewModel: public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("DirViewModel is QML_UNCREATABLE")

  Q_PROPERTY(QString path READ getRoot NOTIFY update);
  Q_PROPERTY(QString size READ getSize NOTIFY update);
  Q_PROPERTY(long long numFiles READ getNumFiles NOTIFY update);
  Q_PROPERTY(FileListView * fileList READ getFileListView CONSTANT);
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
    const auto size = snapshot->info.volumn;
    return QString::fromStdString(std::format("{:2A}", size));
  }

  long long getNumFiles() const {
    return static_cast<long long>(snapshot->children.size());
  }

  FileListView *getFileListView() {
    view_details::get_logger()->info("&flv: {}", (size_t)&flv);
    return &flv;
  }

signals:
  void update();

private:
  data::DirView dv;
  SortUtil sorter;
  /**
   * @warning access snapshot ONLY via GUI thread
   */
  std::unique_ptr<SnapshotT> snapshot;
  FileListView flv;
  
  std::atomic<bool> updateSignal{false};
  /**
   * @warning access freq ONLY via runner
   */
  FreqUtils freq{.name = "DirViewModel UI Freq"};
  playground::runner<async::cancellable_function<void>> runner;
  
  std::stop_source stop;
  async::task_future<void> eventPollFuture;
  std::thread searchRunner;

  auto eventPoll(std::stop_token token) -> async::co_task;

  SnapshotT getNewSnapshot();

  /**
   * @warning access ONLY view GUI thread
   */
  void updateSnapshot(std::unique_ptr<SnapshotT> newSnapshot);
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

  Q_INVOKABLE void setPath() {
    const auto path = data::fs::path{"/Users/lym01803"};
    dvm = std::make_unique<DirViewModel>(data::DirView{path}, nullptr);
    QQmlEngine::setObjectOwnership(dvm.get(), QQmlEngine::CppOwnership);
    setState(State::SelectedPath);
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
};

