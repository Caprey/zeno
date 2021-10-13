import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.12

Rectangle {
    id: thisScene
    anchors.fill: parent
    color: '#222'

    property var selectedChildren: []

    function doSelect(item, multiselect) {
        if (item == null) {
            for (var i in selectedChildren)
                selectedChildren[i].selected = false
            selectedChildren = []
        } else if (multiselect) {
            if (!selectedChildren.includes(item)) {
                selectedChildren.push(item)
                item.selected = true
            } else {
                selectedChildren.remove(item)
                item.selected = false
            }
        } else {
            if (selectedChildren.length == 1 && selectedChildren.includes(item)) {
                for (var i in selectedChildren)
                    selectedChildren[i].selected = false
                selectedChildren = []
                item.selected = false
            } else {
                for (var i in selectedChildren)
                    selectedChildren[i].selected = false
                selectedChildren = [item]
                item.selected = true
            }
        }
    }

    Component {
        id: compZenoNode
        ZenoNode {}
    }

    Flickable {
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: true

        MouseArea {
            anchors.fill: parent

            onClicked: {
                thisScene.doSelect(null)
            }
        }

        Component.onCompleted: {
            compZenoNode.createObject(this, {scene: thisScene});
        }
    }
}
