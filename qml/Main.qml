pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
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

      FolderDialog {
        id: dialog
        title: "选择根目录"
        
        onAccepted: {
          entrance.setPath(selectedFolder);
        }
        onRejected: {}
      }

      Button {
        id: selectButton
        Layout.alignment: Qt.AlignHCenter
        topPadding: 15
        bottomPadding:15
        leftPadding: 15
        rightPadding: 15

        text: "选择根目录"
        font.pointSize: 24
        onClicked: { dialog.open() }
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
