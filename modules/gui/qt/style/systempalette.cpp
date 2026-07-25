/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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
#include "systempalette.hpp"
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <maininterface/mainctx.hpp>
#include <vlc_modules.h>
#include <vlc_picture.h>

namespace {

QColor setColorAlpha(const QColor& c1, float alpha)
{
    QColor c(c1);
    c.setAlphaF(alpha);
    return c;
}

#ifndef _WIN32
/**
 * function taken from QtBase gui/platform/unix/qgenericunixservices.cpp
 */
static inline QByteArray detectLinuxDesktopEnvironment()
{
    const QByteArray xdgCurrentDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    if (!xdgCurrentDesktop.isEmpty())
        return xdgCurrentDesktop.toUpper(); // KDE, GNOME, UNITY, LXDE, MATE, XFCE...

    // Classic fallbacks
    if (!qEnvironmentVariableIsEmpty("KDE_FULL_SESSION"))
        return QByteArrayLiteral("KDE");
    if (!qEnvironmentVariableIsEmpty("GNOME_DESKTOP_SESSION_ID"))
        return QByteArrayLiteral("GNOME");

    // Fallback to checking $DESKTOP_SESSION (unreliable)
    QByteArray desktopSession = qgetenv("DESKTOP_SESSION");

    // This can be a path in /usr/share/xsessions
    int slash = desktopSession.lastIndexOf('/');
    if (slash != -1) {
        QSettings desktopFile(QFile::decodeName(desktopSession + ".desktop"), QSettings::IniFormat);
        desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
        QByteArray desktopName = desktopFile.value(QStringLiteral("DesktopNames")).toByteArray();
        if (!desktopName.isEmpty())
            return desktopName;

        // try decoding just the basename
        desktopSession = desktopSession.mid(slash + 1);
    }

    if (desktopSession == "gnome")
        return QByteArrayLiteral("GNOME");
    else if (desktopSession == "xfce")
        return QByteArrayLiteral("XFCE");
    else if (desktopSession == "kde")
        return QByteArrayLiteral("KDE");

    return QByteArrayLiteral("UNKNOWN");
}

bool isGTKBasedEnvironment()
{
    QList<QByteArray> gtkBasedEnvironments{
        "GNOME",
        "X-CINNAMON",
        "UNITY",
        "MATE",
        "XFCE",
        "LXDE"
    };
    const QList<QByteArray> desktopNames = detectLinuxDesktopEnvironment().split(':');
    for (const QByteArray& desktopName: desktopNames)
    {
        if (gtkBasedEnvironments.contains(desktopName))
            return true;
    }
    return false;
}
#endif


static void PaletteChangedCallback(vlc_qt_theme_provider_t*, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    emit priv->paletteChanged();
}

static void MetricsChangedCallback(vlc_qt_theme_provider_t*, vlc_qt_theme_image_type type, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    assert(priv);
    priv->updateMetrics(type);
}


static void ReleaseVLCPictureCb(void* data)
{
    auto pic = static_cast<picture_t*>(data);
    if (pic)
        picture_Release(pic);
}


static void setColorRBGAInt(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    int r, int g, int b, int a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

static void setColorRBGAFloat(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    double r, double g, double b, double a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color;
    color.setRgbF(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

}

//ExternalPaletteImpl

ExternalPaletteImpl::ExternalPaletteImpl(MainCtx* ctx, SystemPalette& palette, QObject* parent)
    : QObject(parent)
    , m_palette(palette)
    , m_ctx(ctx)
{
}

ExternalPaletteImpl::~ExternalPaletteImpl()
{
    if (m_provider)
    {
        if (m_provider->close)
            m_provider->close(m_provider);
        if (m_module)
            module_unneed(m_provider, m_module);
        vlc_object_delete(m_provider);
    }
}

bool ExternalPaletteImpl::init()
{
    QString preferedProvider;
#ifndef _WIN32
    if (isGTKBasedEnvironment())
        preferedProvider = "qt-themeprovider-gtk";
#endif

    m_provider = vlc_object_create<vlc_qt_theme_provider_t>(m_ctx->getIntf());
    if (!m_provider)
        return false;


    m_provider->paletteUpdated = PaletteChangedCallback;
    m_provider->paletteUpdatedData = this;

    m_provider->metricsUpdated = MetricsChangedCallback;
    m_provider->metricsUpdatedData = this;

    m_provider->setColorF = setColorRBGAFloat;
    m_provider->setColorInt = setColorRBGAInt;
    m_provider->setColorData = &m_palette;

    m_module = module_need(m_provider, "qt theme provider",
                           preferedProvider.isNull() ? nullptr : qtu(preferedProvider),
                           true);
    if (!m_module)
        return false;
    return true;
}

bool ExternalPaletteImpl::isThemeDark() const
{
    if (!m_provider->isThemeDark)
        return false;
    return m_provider->isThemeDark(m_provider);
}

bool ExternalPaletteImpl::hasCSDImages() const
{
    if (!m_provider->supportThemeImage)
        return false;
    return m_provider->supportThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON);
}


QImage ExternalPaletteImpl::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!m_provider->getThemeImage)
        return {};
    vlc_qt_theme_image_setting imageSettings;
    imageSettings.windowScaleFactor = m_ctx->intfMainWindow()->devicePixelRatio();
    imageSettings.userScaleFacor = m_ctx->getIntfUserScaleFactor();
    imageSettings.u.csdButton.buttonType = type;
    imageSettings.u.csdButton.state = state;
    imageSettings.u.csdButton.maximized = maximized;
    imageSettings.u.csdButton.active = active;
    imageSettings.u.csdButton.bannerHeight = bannerHeight;
    picture_t* pic = m_provider->getThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &imageSettings);
    if (!pic)
        return {};

    QImage::Format format = QImage::Format_Invalid;
    switch (pic->format.i_chroma)
    {
    case VLC_CODEC_ARGB:
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    case VLC_CODEC_RGBA:
        format = QImage::Format_RGBA8888_Premultiplied;
        break;
    case VLC_CODEC_RGB24:
        format = QImage::Format_RGB888;
        break;
    default:
        msg_Err(m_ctx->getIntf(), "unexpected image format from theme provider");
        break;
    }

    return QImage(pic->p[0].p_pixels,
            pic->format.i_visible_width, pic->format.i_visible_height, pic->p[0].i_pitch,
            format,
            ReleaseVLCPictureCb, pic
            );
}

CSDMetrics* ExternalPaletteImpl::getCSDMetrics() const
{
    return m_csdMetrics.get();
}

int ExternalPaletteImpl::update()
{
    if (m_provider->updatePalette)
        return m_provider->updatePalette(m_provider);
    return VLC_EGENERIC;
}

void ExternalPaletteImpl::updateMetrics(vlc_qt_theme_image_type type)
{
    if (m_provider->getThemeMetrics)
    {
        if (type == VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON)
        {
            m_csdMetrics.reset();
            [&](){
                if (!m_provider->getThemeMetrics)
                    return;
                vlc_qt_theme_metrics metrics;
                memset(&metrics, 0, sizeof(metrics));
                bool ret = m_provider->getThemeMetrics(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &metrics);
                if (!ret)
                    return;
                m_csdMetrics = std::make_unique<CSDMetrics>();
                m_csdMetrics->interNavButtonSpacing = metrics.u.csd.interNavButtonSpacing;

                m_csdMetrics->csdFrameMarginLeft = metrics.u.csd.csdFrameMarginLeft;
                m_csdMetrics->csdFrameMarginRight = metrics.u.csd.csdFrameMarginRight;
                m_csdMetrics->csdFrameMarginTop = metrics.u.csd.csdFrameMarginTop;
                m_csdMetrics->csdFrameMarginBottom = metrics.u.csd.csdFrameMarginBottom;
            }();
            emit CSDMetricsChanged();
        }
    }
}

SystemPalette::SystemPalette(QObject* parent)
    : QObject(parent)
{

}

void SystemPalette::componentComplete()
{
    {
        assert(m_ctx);
        char* accentColor = var_InheritString(m_ctx->getIntf(), "qt-accent-color");
        assert(accentColor);

        if (!strcmp(accentColor, "default"))
        {
            // Use default
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        else if (!strcmp(accentColor, "system"))
        {
            assert(qApp);
            const auto palette = qApp->palette();

            orange500 = palette.color(QPalette::Normal, QPalette::Accent);
            orange800 = orange500.lighter(150);
        }
#endif
        else
        {
            orange500 = QColor(accentColor);
            orange800 = orange500.lighter(150);
        }

        free(accentColor);
    }

    updatePalette();
}

ColorSchemeModel::ColorScheme SystemPalette::source() const
{
    return m_source;
}

QImage SystemPalette::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!hasCSDImage())
        return QImage();
    assert(m_palettePriv);
    return m_palettePriv->getCSDImage(type, state, maximized, active, bannerHeight);

}

CSDMetrics* SystemPalette::getCSDMetrics() const
{
    if (!m_palettePriv)
        return nullptr;
    return m_palettePriv->getCSDMetrics();
}

bool SystemPalette::hasCSDImage() const
{
    if (!m_palettePriv)
        return false;
    return m_palettePriv->hasCSDImages();
}

void SystemPalette::setSource(ColorSchemeModel::ColorScheme source)
{
    if (m_source == source)
        return;
    m_source = source;

    updatePalette();
    emit sourceChanged();
}

void SystemPalette::setCtx(MainCtx* ctx)
{
    if (ctx == m_ctx)
        return;
    m_ctx = ctx;
    emit ctxChanged();
    updatePalette();
}

void SystemPalette::updatePalette()
{
    m_palettePriv.reset();
    switch(m_source)
    {
    case ColorSchemeModel::System:
        makeSystemPalette();
        break;
    case ColorSchemeModel::Day:
        makeLightPalette();
        break;
    case ColorSchemeModel::Night:
        makeDarkPalette();
        break;
    default:
        break;
    }

    if (m_palettePriv)
    {
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::paletteChanged,
            this, &SystemPalette::updatePalette);
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::CSDMetricsChanged,
            this, &SystemPalette::CSDMetricsChanged);
    }
    emit paletteChanged();

    bool hasCSDImage = m_palettePriv && m_palettePriv->hasCSDImages();
    if (m_hasCSDImage != hasCSDImage)
    {
        m_hasCSDImage = hasCSDImage;
        emit hasCSDImageChanged();
    }
}

static quint64 makeKey(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                       ColorContext::ColorName name, ColorContext::ColorState state)
{
    static_assert(VQTC_STATE_COUNT < (1<<4), "");
    static_assert(VQTC_SECTION_COUNT < (1<<4), "");
    static_assert(VQTC_NAME_COUNT < (1<<8), "");
    static_assert(VQTC_SET_COUNT < (1<<16), "");
    return  (colorSet << 16)
        + (name << 8)
        + (section << 4)
        + state;
}

void SystemPalette::setColor(ColorContext::ColorSet colorSet,  ColorContext::ColorSection section,
                             ColorContext::ColorName name, ColorContext::ColorState state, QColor color)
{

    quint64 key = makeKey(colorSet, section, name, state);
    m_colorMap[key] = color;
}


QColor SystemPalette::getColor(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                               ColorContext::ColorName name, ColorContext::ColorState state) const
{
    typedef ColorContext C;

    quint64 key = makeKey(colorSet, section, name, state);
    auto it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the normal state
    key = makeKey(colorSet, section, name, C::Normal);
    it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the colorSet View
    //TODO do we want finer hierarchy?
    if (colorSet != C::View)
    {
        return getColor(C::View, section, name, state);
    }
    else
    {
        //nothing matches, that's probably an issue, return an ugly color
        return Qt::magenta;
    }
}


void SystemPalette::makeLightPalette()
{
    m_isDark = false;

    typedef ColorContext C;

    m_colorMap.clear();

    //base set
    //
    //The light theme is built around a bright content area sitting next to a
    //slightly recessed chrome (sidebar, banner, mini player). Separators and
    //borders are hairlines expressed as translucent black so that they blend
    //with whatever surface they are drawn onto instead of being a fixed grey.
    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey200 );

        setColor(CS, C::Fg, C::Primary, C::Normal, darkGrey300);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.25));

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.55));

        //`View` is the fallback color set of `getColor()`, so the highlight
        //roles have to be defined here for every color set that does not
        //provide its own, otherwise the lookup ends up returning magenta.
        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange800, 0.16));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#fde7e9")); //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::red);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#e4dab8")); //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, orange800);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#dff6dd")); //FIXME
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::green);

        //the focus ring is tinted with the accent color rather than being a
        //plain black outline
        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(orange800, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, orange800 );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.16) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::black, 0.35) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.28) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Decoration, C::Separator, C::Normal, setColorAlpha(Qt::black, 0.10));

        //wide, soft shadows: keep the alpha low so elevation reads as a
        //diffuse halo instead of a hard drop shadow
        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.16));

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange800);
        setColor(CS, C::Fg, C::Link, C::Normal, orange800 /* accent */);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey400);
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey300);
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.10));
        setColor(CS, C::Decoration, C::Separator, C::Normal, setColorAlpha(Qt::black, 0.10));
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.06));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.06));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::black, 0.11));

        //selected sidebar entries are filled with the accent color
        setColor(CS, C::Bg, C::Highlight, C::Normal, orange800);
        setColor(CS, C::Bg, C::Highlight, C::Focused, orange800);
        setColor(CS, C::Bg, C::Highlight, C::Hovered, orange800);
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);
    }

    //tool button
    //
    //Toolbar buttons are chrome-less until pointed at, then they gain a
    //translucent rounded fill. Translucency is used on purpose so the fill
    //also works when the button sits on top of artwork or video.
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.07));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.07));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::black, 0.12));
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey400);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.08));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.08));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
    }

    //Item
    //
    //Hovering only lightly tints the row, while the selection is a soft accent
    //wash, so that a selected row still reads as text on a light background.
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.06));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.06));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::black, 0.10));

        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange800, 0.16));
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(orange800, 0.24));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(orange800, 0.24));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.5));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#9e9e9e")); //FIXME not a predef

    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#e65609"));  //FIXME not a predef
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    //
    //Push buttons carry a light bezel. It is expressed with translucent black
    //rather than an opaque grey so the very same button also works when it is
    //placed over artwork or video (overlay action buttons).
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.06));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.11));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.11));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::black, 0.17));
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.04));

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.8));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.10));
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::black, 0.10));
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.10));
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey100);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.12));
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.12));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::black, 0.18));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::black, 0.18));
        setColor(CS, C::Bg, C::Secondary, C::Normal, setColorAlpha(lightGrey400, 0.2));
        setColor(CS, C::Fg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::white);
        //highlighted entry of the drop down list
        setColor(CS, C::Bg, C::Secondary, C::Normal, setColorAlpha(orange800, 0.18));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.16));
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.28));
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.18) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.30) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        //text selection uses a translucent accent wash so the glyphs stay readable
        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange800, 0.32));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.10));
        //the knob is a light disc on both states, the track carries the state
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.14));

        setColor(CS, C::Bg, C::Secondary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //SpinBox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.18) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.30) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange800, 0.32));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);
    }
}

void SystemPalette::makeDarkPalette()
{
    m_isDark = true;

    m_colorMap.clear();

    typedef ColorContext C;

    //base set
    //
    //Mirror image of the light theme: the content area is the darkest surface
    //and the chrome around it is raised by a couple of steps, which keeps the
    //sidebar readable without needing a hard separator.
    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey200 );
        setColor(CS, C::Bg, C::Secondary, C::Normal, darkGrey500 );

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.55));

        //`View` is the fallback color set of `getColor()`, so the highlight
        //roles have to be defined here for every color set that does not
        //provide its own, otherwise the lookup ends up returning magenta.
        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange500, 0.22));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Bg, C::Negative, C::Normal, QColor(Qt::darkRed));
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::red);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor(Qt::darkYellow));
        setColor(CS, C::Fg, C::Neutral, C::Normal, orange500);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor(Qt::darkGreen));
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::green);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(orange500, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, orange500 );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.16) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::white, 0.38) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.30) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        //shadows need more punch on dark surfaces to read at all
        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.45));

        setColor(CS, C::Decoration, C::Separator, C::Normal, setColorAlpha(Qt::white, 0.13));

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange500);
        setColor(CS, C::Fg, C::Link, C::Normal, orange500);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey500);
        setColor(CS, C::Bg, C::Secondary, C::Normal, darkGrey600);
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.13));
        setColor(CS, C::Decoration, C::Separator, C::Normal, setColorAlpha(Qt::white, 0.13));
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.09));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.09));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::white, 0.15));

        //selected sidebar entries are filled with the accent color
        setColor(CS, C::Bg, C::Highlight, C::Normal, orange500);
        setColor(CS, C::Bg, C::Highlight, C::Focused, orange500);
        setColor(CS, C::Bg, C::Highlight, C::Hovered, orange500);
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.10));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.10));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::white, 0.16));
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.11));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.11));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.08));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.08));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::white, 0.13));

        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange500, 0.22));
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(orange500, 0.30));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(orange500, 0.30));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.5));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#666666"));  //FIXME not a predef
    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#e67a30"));  //FIXME not a predef
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.10));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.16));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.16));
        setColor(CS, C::Bg, C::Primary, C::Pressed, setColorAlpha(Qt::white, 0.24));
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.06));

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.12));
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::white, 0.12));
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.12));
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey600);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.16));
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.20));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.32));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.32));

        setColor(CS, C::Fg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey500);
        //highlighted entry of the drop down list
        setColor(CS, C::Bg, C::Secondary, C::Normal, setColorAlpha(orange500, 0.26));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.16));
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.30));
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.08));

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.18) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.32) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange500, 0.38));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.16));
        //the knob is a light disc on both states, the track carries the state
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.85));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.16));
        setColor(CS, C::Bg, C::Secondary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.08));

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.18) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.32) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, setColorAlpha(orange500, 0.38));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);
    }
}

void SystemPalette::makeSystemPalette()
{
    if (!m_ctx)
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    auto palette = std::make_unique<ExternalPaletteImpl>(m_ctx, *this);
    if (!palette->init())
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    m_colorMap.clear();
    int ret = palette->update();
    if (ret != VLC_SUCCESS)
    {
        if (palette->isThemeDark())
            makeDarkPalette();
        else
            makeLightPalette();
    }
    else
    {
        m_isDark = palette->isThemeDark();
    }

    m_palettePriv = std::move(palette);
}
