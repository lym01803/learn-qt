pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import learn_qt 1.0

Window {
  id: window
  visible: true
  minimumWidth: 800
  minimumHeight: 600
  title: "Main Window"

  MainEntrance { id : entrance }

  Component {
    id: initComponent
    ColumnLayout {
      anchors.fill: parent

      Button {
        Layout.alignment: Qt.AlignHCenter
        topPadding: 15
        bottomPadding:15
        leftPadding: 15
        rightPadding: 15

        text: "选择路径"
        font.pointSize: 24
        onClicked: {
          entrance.setPath();
        }
      }
    }
  }

  Component {
    id: selectedPathComponent
    Dir {
      vm: entrance.dirViewModel
    }
  }

  Loader {
    id: mainLoader
    anchors.fill: parent 
    sourceComponent: {
      switch (entrance.state) {
        case MainEntrance.Init: return initComponent;
        case MainEntrance.SelectedPath: return selectedPathComponent;
        default: return null;
      }
    }
  }
}
