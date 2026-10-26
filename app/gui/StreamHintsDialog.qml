import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

import SdlGamepadKeyNavigation 1.0

// Shown just before a stream starts to teach the user the control hotkeys.
// The list lives here rather than in C++ so lupdate picks the descriptions up
// together with the rest of the QML strings.
NavigableDialog {
    id: dialog

    // Set when the user asks not to see the hints again. The caller persists
    // the preference, since this dialog doesn't own the streaming preferences.
    property bool dontShowAgain: false

    // The shortcuts handled by SdlInputHandler::performSpecialKeyCombo().
    // All of them work on Windows and macOS, while a few are only enabled
    // when a desktop environment is present.
    property var shortcuts: [
        { keys: "Ctrl+Alt+Shift+Q", desc: qsTr("Quit the stream") },
        { keys: "Ctrl+Alt+Shift+E", desc: qsTr("Quit the stream and close Moonlight") },
        { keys: "Ctrl+Alt+Shift+Z", desc: qsTr("Release the captured mouse and keyboard") },
        { keys: "Ctrl+Alt+Shift+X", desc: qsTr("Toggle full-screen and windowed mode") },
        { keys: "Ctrl+Alt+Shift+D", desc: qsTr("Minimize the stream window") },
        { keys: "Ctrl+Alt+Shift+K", desc: qsTr("Capture or release the system keys") },
        { keys: "Ctrl+Alt+Shift+M", desc: qsTr("Toggle remote desktop mouse mode") },
        { keys: "Ctrl+Alt+Shift+C", desc: qsTr("Show or hide the mouse cursor") },
        { keys: "Ctrl+Alt+Shift+L", desc: qsTr("Lock the pointer inside the video region") },
        { keys: "Ctrl+Alt+Shift+S", desc: qsTr("Show or hide performance statistics") },
        { keys: "Ctrl+Alt+Shift+V", desc: qsTr("Type the clipboard text on the host") },
        { keys: "Ctrl+Alt+Shift+A", desc: qsTr("Turn mouse sensitivity adjustment on or off") },
        { keys: "Ctrl+Alt+Shift+Up/Down", desc: qsTr("Change the mouse sensitivity") }
    ]

    onOpened: {
        // Enable Tab based navigation, so a gamepad can move between the
        // checkbox and the button of this dialog.
        SdlGamepadKeyNavigation.setUiNavMode(true)

        gotItButton.forceActiveFocus(Qt.TabFocus)
    }

    onClosed: {
        SdlGamepadKeyNavigation.setUiNavMode(false)
    }

    ColumnLayout {
        spacing: 10

        Label {
            text: qsTr("You can use these keyboard shortcuts while streaming:")
            font.bold: true
            wrapMode: Text.Wrap
            Layout.maximumWidth: 470
        }

        Column {
            spacing: 4

            Repeater {
                model: dialog.shortcuts

                Row {
                    spacing: 10

                    Label {
                        // Reserve a common width so the descriptions line up
                        width: 190
                        text: modelData.keys
                        color: "#9CDCFE"
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: 270
                        text: modelData.desc
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        CheckBox {
            id: dontShowAgainCheck
            text: qsTr("Do not show this again")
            font.pointSize: 12
            onCheckedChanged: {
                dialog.dontShowAgain = checked
            }
        }
    }

    footer: RowLayout {
        spacing: 8

        Item {
            Layout.fillWidth: true
        }

        Button {
            id: gotItButton
            text: qsTr("Got it")
            flat: true
            onClicked: dialog.accept()

            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            Keys.onUpPressed: {
                dontShowAgainCheck.forceActiveFocus(Qt.TabFocus)
            }
        }
    }

    // Clears the checkbox before the dialog is shown for a new stream. The
    // dialog instance outlives a single stream, so the state of the previous
    // session must not leak into the next one.
    function resetState()
    {
        dontShowAgain = false
        dontShowAgainCheck.checked = false
    }
}
