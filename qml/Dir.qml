pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import learn_qt 1.0

ColumnLayout {
  id: column
  required property DirViewModel vm

  Label {
    Layout.alignment: Qt.AlignTop
    font.pointSize: 24
    topPadding: 8
    bottomPadding: 8
    leftPadding: 16
    rightPadding: 16

    text: column.vm.path
  }

  RowLayout {
    Layout.alignment: Qt.AlignTop
    Label {
      Layout.alignment: Qt.AlignLeft
      font.pointSize: 24
      topPadding: 8
      bottomPadding: 8
      leftPadding: 16
      rightPadding: 8

      text: "Size:"
    }
    Label {
      Layout.alignment: Qt.AlignLeft
      font.pointSize: 24
      topPadding: 8
      bottomPadding: 8
      leftPadding: 8
      rightPadding: 16

      text: column.vm.size
    }
    Label {
      Layout.alignment: Qt.AlignLeft
      font.pointSize: 24
      topPadding: 8
      bottomPadding: 8
      leftPadding: 8
      rightPadding: 16

      text: `文件数量: ${column.vm.numFiles}`
    }
  }

  ListView {
    id: list
    Layout.alignment: Qt.AlignTop
    Layout.fillHeight: true
    Layout.fillWidth: true

    ScrollBar.vertical: ScrollBar { active: true }

    model: column.vm.fileList
    cacheBuffer: list.height
    clip: true

    delegate: Item {
      id: item
      width: list.width
      height: 40

      required property string path  
      required property string size
      required property real ratio
      required property bool isDir
      
      RowLayout {
        anchors.fill: parent
        Label {
          Layout.alignment: Qt.AlignLeft
          font.pointSize: 16
          topPadding: 8
          bottomPadding: 8
          leftPadding: 8
          rightPadding: 8

          text: item.path
        }

        Label {
          Layout.alignment: Qt.AlignLeft
          font.pointSize: 16
          topPadding: 8
          bottomPadding: 8
          leftPadding: 8
          rightPadding: 8

          text: `大小: ${item.size}`
        }

        Label {
          Layout.alignment: Qt.AlignLeft
          font.pointSize: 16
          topPadding: 8
          bottomPadding: 8
          leftPadding: 8
          rightPadding: 8

          text: `占比: ${item.ratio}`
        }

        Label {
          Layout.alignment: Qt.AlignLeft
          font.pointSize: 16
          topPadding: 8
          bottomPadding: 8
          leftPadding: 8
          rightPadding: 8

          text: `是文件夹: ${item.isDir ? "Y" : "N"}`
        }
      }
    }
  }

}
