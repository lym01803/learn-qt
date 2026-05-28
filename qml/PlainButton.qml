pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as BasicCtrl

BasicCtrl.Button {
  topPadding: 4
  bottomPadding: 4
  leftPadding: 8
  rightPadding: 8

  background: Rectangle {
    border.width: 1
    border.color: "#e0e0e0"
    color: (parent.enabled && parent.hovered) ? "#eeeeee" : "#ffffff"
    radius: 6
  }

  opacity: {
    if (enabled) {
      return pressed ? 0.6 : 1.0;
    }
    return 0.4;
  }
}
