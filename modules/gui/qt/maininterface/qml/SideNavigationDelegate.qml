/*****************************************************************************
 * Copyright (C) 2025 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts


import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Style
import VLC.Util

T.ItemDelegate {
    id: control

    // Properties

    property string iconTxt: ""

    property bool showText: true

    // Settings

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: VLCStyle.margin_xxsmall

    // Accessible

    Accessible.onPressAction: control.clicked()

    // Tooltip

    T.ToolTip.visible: (showText === false && T.ToolTip.text && (hovered || visualFocus))

    T.ToolTip.delay: VLCStyle.delayToolTipAppear

    T.ToolTip.text: text

    // Childs

    ColorContext {
        id: theme
        colorSet: ColorContext.TabButton

        focused: control.visualFocus
        hovered: control.hovered
        pressed: control.down
        enabled: control.enabled
    }

    // The selection is drawn as a rounded "pill" inset from the pane edges
    // rather than a full width bar, which keeps the sidebar reading as a list
    // of items instead of a stack of stripes.
    background: Item {
        Widgets.AnimatedBackground {
            anchors.fill: parent

            // The pill is kept `sideNavigation_itemMargin` inside the content,
            // which is itself already inset by the window safe area. Deriving
            // the margins from the paddings is what keeps the pill out of the
            // unsafe area.
            anchors.topMargin: Math.round(VLCStyle.sideNavigation_itemMargin / 2)
            anchors.bottomMargin: Math.round(VLCStyle.sideNavigation_itemMargin / 2)
            anchors.leftMargin: Math.max(0, control.leftPadding - VLCStyle.sideNavigation_itemMargin)
            anchors.rightMargin: Math.max(0, control.rightPadding - VLCStyle.sideNavigation_itemMargin)

            enabled: theme.initialized

            radius: VLCStyle.sideNavigation_itemRadius

            color: control.checked ? theme.bg.highlight : theme.bg.primary

            border.color: control.visualFocus ? theme.visualFocus
                                              : Qt.alpha(theme.visualFocus, 0.0)
        }
    }

    contentItem: RowLayout {
        spacing: VLCStyle.margin_xsmall

        Item {
            Layout.preferredWidth: VLCStyle.icon_banner
            Layout.fillHeight: true

            Widgets.IconLabel {
                id: iconLabel

                visible: text.length > 0

                anchors.centerIn: parent

                text: control.iconTxt

                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter

                color: {
                    if (control.checked)
                        return theme.fg.highlight
                    if (control.highlighted)
                        return theme.accent
                    return theme.fg.primary
                }

                font.pixelSize: VLCStyle.icon_banner

                Behavior on color {
                    enabled: theme.initialized

                    ColorAnimation {
                        duration: VLCStyle.duration_short
                    }
                }
            }
        }

        T.Label {
            id: label

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: control.text

            verticalAlignment: Text.AlignVCenter

            color: control.checked ? theme.fg.highlight : theme.fg.primary

            elide: Text.ElideRight

            font.pixelSize: VLCStyle.fontSize_normal

            font.weight: (control.checked || control.highlighted) ? Font.DemiBold
                                                                  : Font.Normal

            //button text is already exposed
            Accessible.ignored: true

            Behavior on color {
                enabled: theme.initialized

                ColorAnimation {
                    duration: VLCStyle.duration_short
                }
            }
        }
    }
}
