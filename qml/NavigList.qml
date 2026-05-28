pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as BasicCtrl
import QtQuick.Layouts
import learn_qt 1.0

Flow {
  id: navigFlow
  spacing: 0
  flow: Flow.LeftToRight

  required property NavigListView nvl
  required property var onClickedAction

  Repeater {
    model: navigFlow.nvl

    delegate: Row {
      id: item 
      required property string path
      required property int order 

      BasicCtrl.Button {
        id: button
        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0

        font.pointSize: 20
        text: item.path
        onClicked: navigFlow.onClickedAction(item.order)

        background: Rectangle {
          border.width: 0
          color: parent.hovered ? "#f8f8f8" : "#ffffff"
        }

        opacity: pressed ? 0.6 : 1.0;

        contentItem: Item {
          id: textItem
          property real maxWidth: navigFlow.width * 0.5 - 10
          property real fontSize: 20

          implicitWidth: Math.min(textItem.maxWidth, innerText.implicitWidth)
          implicitHeight: innerText.implicitHeight
          width: implicitWidth
          height: implicitHeight

          TextMetrics {
            id: textMetrics 
            font.pointSize: textItem.fontSize
            elide: Text.ElideMiddle
            elideWidth: textItem.maxWidth            
            text: item.path
          }

          Text {
            id: innerText
            anchors.fill: parent
            topPadding: 4
            bottomPadding: 4
            leftPadding: 0
            rightPadding: 0

            wrapMode: Text.NoWrap
            font.pointSize: textItem.fontSize
            text: textMetrics.elidedText
          }
        }
      }

      Text {
        topPadding: 4
        bottomPadding: 4
        leftPadding: 0
        rightPadding: 0
        font.pointSize: 20
        text: item.order === 0 ? "" : "/"
      }
    }
  }
}
