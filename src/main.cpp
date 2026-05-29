#include "utf8utils.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtCore/qvariant.h>
#include <QtGui/qguiapplication.h>
#include <QtQml/qqmlapplicationengine.h>
#include <datamodel.h>
#include <toy_concurrency/async_tool.h>
#include <toy_concurrency/concurrency_utils.h>
#include <toy_concurrency/toyqueue.h>
#include <utils/logger_utils.h>
#include <view.h>
#include <iostream>

namespace  {

auto &&get_logger() {
  static utils::logger_t logger{"main logger", "log/main.log", 1048576 * 10};
  return logger;
}

}

int main(int argc, char *argv[]) {
  QGuiApplication app{argc, argv};
  QQmlApplicationEngine qml_engine;
  QObject::connect(&qml_engine, &QQmlApplicationEngine::objectCreationFailed, &app, 
    []() {
      get_logger()->info("QQmlApplicationEngine::objectCreationFailed.");
      QGuiApplication::exit(-1);
    }, 
    Qt::QueuedConnection
  );

  qml_engine.loadFromModule("learn_qt", "Main");

  return QGuiApplication::exec();
}
