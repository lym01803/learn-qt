pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as BasicCtrl
import QtQuick.Layouts
// import QtQuick.Effects
import learn_qt 1.0

ColumnLayout {
  id: column
  required property DirViewModel vm

  NavigList {
    Layout.fillWidth: true
    Layout.topMargin: 8
    Layout.bottomMargin: 8
    Layout.leftMargin: 8
    Layout.rightMargin: 8
    Layout.alignment: Qt.AlignTop

    nvl: column.vm.navigList
    onClickedAction: (order) => {
      if (order > 0) {
        column.vm.gotoAncestor(order)
      }
    }
  }

  TextField {
    id: query
    Layout.fillWidth: true 
    Layout.topMargin: 4
    Layout.bottomMargin: 4
    Layout.leftMargin: 8
    Layout.rightMargin: 8

    placeholderText: "Search"
    onTextEdited: {
      column.vm.setQuery(text)
    }
  }

  Connections {
    target: column.vm 
    
    function onClearQuery() {
      query.text = ""
    }
  }

  RowLayout {
    Layout.alignment: Qt.AlignTop
    Layout.leftMargin: 8
    Layout.rightMargin: 8
    Layout.topMargin: 8
    Layout.bottomMargin: 8
    
    PlainButton {
      text: "Back"
      onClicked: {
        column.vm.gotoAncestor(1)
      }
      enabled: !(column.vm.isRoot)
    }

    Item {
      Layout.fillWidth: true
    }

    RowLayout {
      Layout.alignment: Qt.AlignTop
      Label {
        Layout.alignment: Qt.AlignLeft
        font.pointSize: 14
        topPadding: 4
        bottomPadding: 4
        leftPadding: 4
        rightPadding: 4

        text: column.vm.isDir 
            ? `${column.vm.numFiles} 个文件, ${column.vm.size}` 
            : column.vm.size
      }
    }

    Item {
      Layout.fillWidth: true
    }

    PlainButton {
      text: "Open"
      onClicked: {
        column.vm.openDir()
      }
      enabled: column.vm.isDir
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
      height: 34

      // required property string path
      required property string fileName  
      required property string size
      required property real ratio
      required property bool isDir
      required property real id
      
      BasicCtrl.Button {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 20
        anchors.topMargin: 1
        anchors.bottomMargin: 1

        onClicked: {
          column.vm.gotoChild(item.id)
        }

        background: Rectangle {
          id: backRectangle
          radius: 6
          border.width: 1.0
          border.color : parent.hovered ? "#888888" : "#d5d5d5"
          gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
              position: 0.0 
              color: "#e8e8e8"
            }
            GradientStop {
              position: 0.0001 + 0.9997 * item.ratio
              color: "#e8e8e8"
            }
            GradientStop {
              position: 0.0002 + 0.9997 * item.ratio
              color: "#ffffff"
            }
            GradientStop {
              position: 1.0
              color: "#ffffff"
            }
          }
        }

        opacity: pressed ? 0.6 : 1.0

        RowLayout {
          anchors.fill: parent
          anchors.leftMargin: 4
          anchors.rightMargin: 4

          Image {
            id: imageIcon
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: 4
            Layout.rightMargin: 2
            Layout.topMargin: 4
            Layout.bottomMargin: 4

            sourceSize.height: 16
            source: "../resources/icons/folder.svg"
            state: item.isDir ? "Dir" : "File"

            states: [
              State {
                name: "Dir"
                PropertyChanges {
                  imageIcon.source: "../resources/icons/folder.svg"
                  imageIcon.sourceSize.height: 13
                }
              }, 
              State {
                name: "File"
                PropertyChanges {
                  imageIcon.source: "../resources/icons/doc.svg"
                  imageIcon.sourceSize.height: 16
                }
              }
            ]
          }

          Text {
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: 2
            Layout.rightMargin: 4
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            font.pointSize: 14

            text: item.fileName
            elide: Text.ElideMiddle
            Layout.fillWidth: true
          }

          Text {
            Layout.alignment: Qt.AlignLeft
            font.pointSize: 14
            topPadding: 2
            bottomPadding: 2
            leftPadding: 0
            rightPadding: 0

            text: item.size
          }

          Image {
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            Layout.topMargin: 4
            Layout.bottomMargin: 4

            source: "../resources/icons/chevron.right.svg"
            sourceSize.height: 12
          }
        }
      }
    }
  }

  Item {
    Layout.bottomMargin: 10
  }

}
