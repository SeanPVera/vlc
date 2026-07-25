/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts

import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Style

Dialog {
    id: control

    property Item rootWindow: null

    focus: true
    modal: true


    anchors.centerIn: Overlay.overlay

    padding: VLCStyle.margin_normal
    margins: VLCStyle.margin_large

    implicitWidth: contentWidth > 0 ? contentWidth + leftPadding + rightPadding : 0
    implicitHeight: (header && header.visible ? header.implicitHeight + spacing : 0)
                    + (footer && footer.visible ? footer.implicitHeight + spacing : 0)
                    + (contentHeight > 0 ? contentHeight + topPadding + bottomPadding : 0)

    closePolicy: Popup.CloseOnEscape

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        palette: VLCStyle.palette
        colorSet: ColorContext.Window
    }

    Overlay.modal: Item {
        Rectangle {
            anchors.fill: blur

            visible: !blur.visible

            color: theme.bg.primary

            opacity: 0.6
        }

        Widgets.DualKawaseBlur {
            id: blur

            anchors.fill: parent
            anchors.margins: MainCtx.windowExtendedMargin

            visible: MainCtx.backdropBlurRequested() && blur.available

            mode: Widgets.DualKawaseBlur.Mode.SixPass

            source: ShaderEffectSource {
                sourceItem: blur.visible ? control.rootWindow : null
                live: true
                hideSource: true
            }

            radius: 2
        }
    }

    background: Rectangle {
        color: theme.bg.primary

        radius: VLCStyle.popup_radius

        border.color: theme.border
        border.width: VLCStyle.border
        border.pixelAligned: (radius < Number.EPSILON)

        Widgets.RoundedRectangleShadow {
            blurRadius: VLCStyle.dp(24, VLCStyle.scale)

            yOffset: VLCStyle.dp(8, VLCStyle.scale)

            color: theme.shadow
        }
    }

    //FIXME use the right xxxLabel class
    header: T.Label {
        text: control.title
        visible: control.title
        elide: Label.ElideRight
        font.bold: true
        color: theme.fg.primary
        padding: 6
    }

    // Dialogs settle in place instead of just fading, which reads as the
    // sheet being pushed towards the user.
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: VLCStyle.duration_short }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.96; duration: VLCStyle.duration_short; easing.type: Easing.InQuad }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: VLCStyle.duration_long }
        NumberAnimation { property: "scale"; from: 0.94; to: 1.0; duration: VLCStyle.duration_long; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
    }
}
