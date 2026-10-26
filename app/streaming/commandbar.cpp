#include "commandbar.h"

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QQuickWindow>
#include <QScreen>
#include <QtMath>

#include <Limelight.h>

#include "input/input.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

namespace {

// Bar geometry, in logical pixels. The layout is computed in logical pixels
// and rasterized at the display's device pixel ratio, so the bar keeps its
// apparent size on HiDPI displays.
const qreal k_BarHeight = 32;
const qreal k_BarPadding = 3;
const qreal k_ButtonWidth = 34;
const qreal k_ButtonHeight = 26;
const qreal k_ButtonSpacing = 1;
const qreal k_ButtonRadius = 5;
const qreal k_SeparatorWidth = 9;
const qreal k_BarCornerRadius = 8;
const qreal k_BarTopMargin = 8;

// Extra margin around each panel so its drop shadow is not clipped
const qreal k_PanelMargin = 6;

// How long an unpinned bar stays visible after the pointer leaves it, so the
// pointer can travel between the bar and the video
const quint32 k_AutoHideDelay = 350;

// How long the shortcut list stays on screen after a stream starts
const quint32 k_KeyHintsDuration = 12000;

// How long the mouse sensitivity confirmation stays on screen
const quint32 k_StatusDuration = 1500;

// Opacity of the command bar interface. The bar is fully opaque or fully
// hidden; the value is kept as a qreal so painting can fade it later if needed.
const qreal k_FullOpacity = 1.0;

// Height of the strip at the top of the window that reveals the bar
const qreal k_EdgeThreshold = 2;

// Vertical gap between the bar and its tooltip
const qreal k_TooltipGap = 5;

// Font sizes in points
const int k_TooltipFontSize = 8;
const int k_TooltipShortcutFontSize = 8;
const int k_HintTitleFontSize = 11;
const int k_HintFontSize = 9;
const int k_HintShortcutFontSize = 9;
const int k_DialogTitleFontSize = 11;
const int k_DialogFontSize = 9;

// Colors in straight alpha, as expected by every renderer's overlay blending
const QRgb k_ColorPanelTop = 0xF0333333;
const QRgb k_ColorPanelBottom = 0xF01B1B1B;
const QRgb k_ColorPanelBorder = 0xFF4E4E4E;
const QRgb k_ColorSeparator = 0xFF505050;
const QRgb k_ColorButtonHover = 0xFF3C5C8E;
const QRgb k_ColorButtonPressed = 0xFF2B4870;
const QRgb k_ColorButtonActive = 0xFF2E6DA4;
const QRgb k_ColorIcon = 0xFFE8E8E8;
const QRgb k_ColorIconInactive = 0xFF8E8E8E;
const QRgb k_ColorText = 0xFFF2F2F2;
const QRgb k_ColorTextDim = 0xFFB4B4B4;
const QRgb k_ColorShortcut = 0xFF9CC4EA;
const QRgb k_ColorAccent = 0xFF3D7EB8;
const QRgb k_ColorDanger = 0xFFB23232;
const QRgb k_ColorDangerHover = 0xFFD04040;
const QRgb k_ColorNeutralButton = 0xFF3C3C3C;
const QRgb k_ColorNeutralButtonHover = 0xFF4E4E4E;
const QRgb k_ColorCheckBox = 0xFFB8B8B8;
const QRgb k_ColorShadow = 0xB0000000;
const QRgb k_ColorDialogDim = 0x78000000;

enum Icon {
    IconPin,
    IconWindowed,
    IconMinimize,
    IconSensitivity,
    IconHints,
    IconQuit,
};

QColor color(QRgb argb)
{
    return QColor::fromRgba(argb);
}

QFont uiFont(int pointSize, bool bold = false)
{
    QFont font = QGuiApplication::font();
    font.setPointSize(pointSize);
    font.setBold(bold);
    return font;
}

void paintPanel(QPainter& painter, const QRectF& rect, qreal radius, bool accent)
{
    // A soft drop shadow, drawn as a few translucent rounded rectangles so we
    // avoid the cost of a blurred pixmap on every repaint
    painter.setPen(Qt::NoPen);
    for (int i = 4; i > 0; i--) {
        QColor shadow = color(k_ColorShadow);
        shadow.setAlphaF(shadow.alphaF() / (i * 1.7));
        painter.setBrush(shadow);
        painter.drawRoundedRect(rect.adjusted(-i, -i, i, i + i / 2.0), radius + i, radius + i);
    }

    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, color(k_ColorPanelTop));
    gradient.setColorAt(1.0, color(k_ColorPanelBottom));

    painter.setBrush(gradient);
    painter.setPen(QPen(color(accent ? k_ColorAccent : k_ColorPanelBorder), 1.0));
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void paintIcon(QPainter& painter, const QRectF& box, int icon, const QColor& tint)
{
    QPen pen(tint, qBound(1.2, box.width() / 16.0, 2.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF c = box.center();
    const qreal s = box.width() / 2.0;

    switch (icon) {
    case IconPin:
        // Push pin: a filled round head with a shaft below it. The head is
        // always filled, so the pinned state is conveyed by the button color.
        painter.setPen(Qt::NoPen);
        painter.setBrush(tint);
        painter.drawEllipse(c + QPointF(0, -s * 0.4), s * 0.34, s * 0.34);
        painter.setPen(pen);
        painter.drawLine(c + QPointF(0, -s * 0.06), c + QPointF(0, s * 0.58));
        break;

    case IconWindowed:
    {
        const QRectF frame(c.x() - s * 0.74, c.y() - s * 0.62, s * 1.48, s * 1.24);
        painter.drawRect(frame);
        painter.drawLine(QPointF(frame.left(), frame.top() + s * 0.34),
                         QPointF(frame.right(), frame.top() + s * 0.34));
        break;
    }

    case IconMinimize:
        painter.drawLine(QPointF(c.x() - s * 0.58, c.y() + s * 0.42),
                         QPointF(c.x() + s * 0.58, c.y() + s * 0.42));
        break;

    case IconSensitivity:
    {
        // Mouse body plus an up arrow, echoing the sensitivity hotkeys
        const QRectF body(c.x() - s * 0.82, c.y() - s * 0.76, s * 0.96, s * 1.52);
        painter.drawRoundedRect(body, s * 0.46, s * 0.46);
        painter.drawLine(QPointF(body.center().x(), body.top()),
                         QPointF(body.center().x(), body.top() + s * 0.38));

        const qreal arrowX = c.x() + s * 0.76;
        painter.drawLine(QPointF(arrowX, c.y() + s * 0.64), QPointF(arrowX, c.y() - s * 0.54));
        painter.drawLine(QPointF(arrowX, c.y() - s * 0.54),
                         QPointF(arrowX - s * 0.26, c.y() - s * 0.22));
        painter.drawLine(QPointF(arrowX, c.y() - s * 0.54),
                         QPointF(arrowX + s * 0.26, c.y() - s * 0.22));
        break;
    }

    case IconHints:
    {
        const QRectF frame(c.x() - s * 0.86, c.y() - s * 0.42, s * 1.72, s * 0.84);
        painter.drawRoundedRect(frame, s * 0.16, s * 0.16);

        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 3; col++) {
                painter.fillRect(QRectF(frame.left() + s * (0.17 + col * 0.30),
                                        frame.top() + s * (0.17 + row * 0.32),
                                        s * 0.22, s * 0.18),
                                 tint);
            }
        }

        painter.drawLine(QPointF(c.x() - s * 0.52, frame.bottom() + s * 0.22),
                         QPointF(c.x() + s * 0.52, frame.bottom() + s * 0.22));
        break;
    }

    case IconQuit:
    {
        const QRectF ring(c.x() - s * 0.72, c.y() - s * 0.72, s * 1.44, s * 1.44);
        painter.drawArc(ring, 120 * 16, 300 * 16);
        painter.drawLine(QPointF(c.x(), c.y() - s * 1.0), QPointF(c.x(), c.y() - s * 0.16));
        break;
    }

    default:
        break;
    }
}

}

CommandBar* CommandBar::s_CommandBar = nullptr;

CommandBar::CommandBar(QQuickWindow* qtWindow)
    : m_QtWindow(qtWindow),
      m_Session(nullptr),
      m_OverlayManager(nullptr),
      m_Window(nullptr),
      m_WindowWidth(0),
      m_WindowHeight(0),
      m_UiScale(1.0),
      m_Visible(false),
      m_Opacity(0.0),
      m_Pinned(false),
      m_PinnedLocked(false),
      m_HideDeadline(0),
      m_CapturedBeforeRelease(false),
      m_ReleaseForInteraction(false),
      m_PointerOverInterface(false),
      m_HoverAction(Action::None),
      m_PressedAction(Action::None),
      m_Dialog(Dialog::None),
      m_HoverDialogButton(DialogButton::None),
      m_PressedDialogButton(DialogButton::None),
      m_KeyHintsVisible(false),
      m_KeyHintsDeadline(0),
      m_KeyHintsDontShowAgain(false),
      m_HoverHintControl(HintControl::None),
      m_PressedHintControl(HintControl::None),
      m_PrefsShowKeyHintsOnStreamStart(true),
      m_PrefsMouseSensitivityEnabled(true),
      m_PrefsMouseSensitivity(100),
      m_StatusOverlayTimer(0)
{
    layout();
}

CommandBar::~CommandBar()
{
    if (s_CommandBar == this) {
        s_CommandBar = nullptr;
    }

    if (m_StatusOverlayTimer != 0) {
        SDL_RemoveTimer(m_StatusOverlayTimer);
        m_StatusOverlayTimer = 0;
    }

    if (m_OverlayManager != nullptr) {
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBar, false);
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBarTooltip, false);
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBarDialog, false);
        m_OverlayManager->setOverlayState(Overlay::OverlayKeyHints, false);
    }
}

CommandBar* CommandBar::create(QQuickWindow* qtWindow)
{
    delete s_CommandBar;

    s_CommandBar = new CommandBar(qtWindow);
    return s_CommandBar;
}

bool CommandBar::isEnabled()
{
    return StreamingPreferences::get()->commandBarEnabled;
}

//------------------------------------------------------------------------------
// Session notifications
//------------------------------------------------------------------------------

void CommandBar::notifyStreamStarting()
{
    m_Session = Session::get();
    if (m_Session == nullptr) {
        return;
    }

    m_OverlayManager = &m_Session->m_OverlayManager;
    m_Window = m_Session->m_Window;

    reloadPreferences();
    layout();

    // A bar the user pinned comes back pinned, but only in full-screen mode
    if (m_Pinned && isFullScreen()) {
        reveal();
    }

    // Announce the keyboard shortcuts once at the start of the stream, unless
    // the user asked us to stop doing that
    if (m_PrefsShowKeyHintsOnStreamStart) {
        showKeyHints();
    }
}

void CommandBar::notifyWindowChanged()
{
    if (m_Session == nullptr) {
        return;
    }

    m_Window = m_Session->m_Window;
    layout();

    if (!isFullScreen()) {
        // The bar belongs to full-screen mode, so entering windowed mode hides
        // it. This also releases the pointer, so the user can grab the window
        // frame right after pressing the button.
        if (m_Visible) {
            hide();
        }

        hideKeyHints();
        return;
    }

    // Back in full-screen: a pinned bar reappears, an unpinned bar waits for
    // the pointer or the hotkey.
    if (m_Pinned) {
        reveal();
    }
    else {
        redraw();
    }
}

void CommandBar::reloadPreferences()
{
    StreamingPreferences* prefs = StreamingPreferences::get();

    m_PrefsShowKeyHintsOnStreamStart = prefs->showKeyHintsOnStreamStart;
    m_PrefsMouseSensitivityEnabled = prefs->mouseSensitivityEnabled;
    m_PrefsMouseSensitivity = prefs->mouseSensitivity;

    if (!m_PinnedLocked) {
        m_Pinned = prefs->commandBarPinned;
    }
}

bool CommandBar::isFullScreen() const
{
    if (m_Window == nullptr) {
        return false;
    }

    return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

bool CommandBar::isPointerReleased() const
{
    // In captured relative mode the cursor has no position inside the window,
    // so there is nothing to hover, click, or watch at the window edge.
    return !SDL_GetRelativeMouseMode();
}

//------------------------------------------------------------------------------
// Geometry
//------------------------------------------------------------------------------

void CommandBar::layout()
{
    resetGeometry();

    if (m_Window == nullptr) {
        return;
    }

    int width, height;
    SDL_GetWindowSize(m_Window, &width, &height);
    m_WindowWidth = width;
    m_WindowHeight = height;

    // The whole layout is expressed in window points, which is the same unit
    // that SDL uses for mouse events. The overlay surfaces are rasterized at
    // the display's device pixel ratio so they stay sharp on HiDPI screens.
    qreal scale = 1.0;
    if (m_QtWindow != nullptr) {
        QScreen* screen = m_QtWindow->screen();
        if (screen != nullptr) {
            scale = screen->devicePixelRatio();
        }
    }

    if (!qIsFinite(scale) || scale <= 0.0) {
        scale = 1.0;
    }

    m_UiScale = scale;

    const qreal logicalWidth = m_WindowWidth;

    const int buttonCount = (int)Action::Count;

    // Width of every button plus the padding, the gaps between them, and the
    // separator slot that follows the sensitivity button
    const qreal barWidth = k_BarPadding * 2
                           + buttonCount * k_ButtonWidth
                           + (buttonCount - 1) * k_ButtonSpacing
                           + k_SeparatorWidth;

    // The bar slides up behind the top edge of the window as it fades out
    const qreal barY = k_BarTopMargin - (1.0 - m_Opacity) * (k_BarHeight + k_BarTopMargin);
    const qreal barX = qMax((qreal)0.0, (logicalWidth - barWidth) / 2.0);

    m_BarRect = QRectF(barX, barY, barWidth, k_BarHeight);

    qreal x = m_BarRect.left() + k_BarPadding;
    for (int i = 0; i < buttonCount; i++) {
        if (i == (int)Action::MouseSensitivity) {
            // Separator between the window controls and the stream controls.
            // It occupies its own slot so the buttons keep their positions
            // whether or not the separator is drawn.
            m_SeparatorRect = QRectF(x + k_ButtonSpacing + k_SeparatorWidth / 2.0 - 0.5,
                                     m_BarRect.top() + 7,
                                     1.0, k_BarHeight - 14);
            x += k_SeparatorWidth;
        }

        m_ButtonRects.append(QRectF(x,
                                    m_BarRect.top() + (k_BarHeight - k_ButtonHeight) / 2.0,
                                    k_ButtonWidth, k_ButtonHeight));

        x += k_ButtonWidth + k_ButtonSpacing;
    }
}

void CommandBar::resetGeometry()
{
    m_BarRect = QRectF();
    m_ButtonRects.clear();
    m_SeparatorRect = QRectF();
    m_TooltipRect = QRectF();
    m_DialogRect = QRectF();
    m_DialogButtonRects.clear();
    m_KeyHintsRect = QRectF();
    m_KeyHintsCheckBoxRect = QRectF();
    m_KeyHintsCloseRect = QRectF();
    m_KeyHintsGotItRect = QRectF();
}

QVector<CommandBar::ButtonSpec> CommandBar::buildButtonSpecs() const
{
    QVector<ButtonSpec> specs((int)Action::Count);

    specs[(int)Action::Pin] = {
        Action::Pin,
        m_Pinned ? tr("Unpin the command bar")
                 : tr("Keep the command bar on screen"),
        QString(), false, m_Pinned };

    specs[(int)Action::Windowed] = {
        Action::Windowed,
        tr("Switch to windowed mode (the command bar is hidden while windowed)"),
        QStringLiteral("Ctrl+Alt+Shift+X"), true, false };

    specs[(int)Action::Minimize] = {
        Action::Minimize,
        tr("Minimize the stream window"),
        QStringLiteral("Ctrl+Alt+Shift+D"), true, false };

    specs[(int)Action::MouseSensitivity] = {
        Action::MouseSensitivity,
        m_PrefsMouseSensitivityEnabled
            ? tr("Adjust mouse sensitivity, currently %1% (scroll to change it)")
                  .arg(m_PrefsMouseSensitivity)
            : tr("Mouse sensitivity adjustment is off"),
        QStringLiteral("Ctrl+Alt+Shift+A"), true, m_PrefsMouseSensitivityEnabled };

    specs[(int)Action::KeyHints] = {
        Action::KeyHints,
        m_KeyHintsVisible ? tr("Hide the keyboard shortcut list")
                          : tr("Show the keyboard shortcut list"),
        QString(), false, false };

    specs[(int)Action::QuitStream] = {
        Action::QuitStream,
        tr("Quit the stream"),
        QStringLiteral("Ctrl+Alt+Shift+Q"), true, false };

    return specs;
}

int CommandBar::iconForAction(Action action) const
{
    switch (action) {
    case Action::Pin:
        return IconPin;
    case Action::Windowed:
        return IconWindowed;
    case Action::Minimize:
        return IconMinimize;
    case Action::MouseSensitivity:
        return IconSensitivity;
    case Action::KeyHints:
        return IconHints;
    case Action::QuitStream:
        return IconQuit;
    default:
        return IconPin;
    }
}

QStringList CommandBar::buildKeyHintLines() const
{
    QStringList lines;

    const QString sep = QStringLiteral("  ");

    lines.append(QStringLiteral("Ctrl+Alt+Shift+Q") + sep + tr("Quit the stream"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+E") + sep + tr("Quit the stream and close Moonlight"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+B") + sep + tr("Show or hide the command bar"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+X") + sep + tr("Toggle full-screen and windowed mode"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+D") + sep + tr("Minimize the stream window"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+Z") + sep + tr("Release the mouse and keyboard"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+A") + sep + tr("Turn mouse sensitivity adjustment on or off"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+Up/Down") + sep + tr("Change the mouse sensitivity"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+S") + sep + tr("Show or hide performance statistics"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+M") + sep + tr("Toggle remote desktop mouse mode"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+V") + sep + tr("Type the clipboard text on the host"));
    lines.append(QStringLiteral("Ctrl+Alt+Shift+L") + sep + tr("Lock the pointer inside the video region"));

    return lines;
}

//------------------------------------------------------------------------------
// Visibility
//------------------------------------------------------------------------------

void CommandBar::reveal()
{
    if (!isEnabled() || !isFullScreen()) {
        return;
    }

    m_HideDeadline = 0;

    if (!m_Visible) {
        m_Visible = true;
        updateCaptureState();
    }

    // Show the bar immediately. Animating it would only advance while input
    // events keep arriving, which can appear to hang on a quiet stream.
    m_Opacity = k_FullOpacity;

    redraw();
}

void CommandBar::hide()
{
    if (!m_Visible && m_Dialog == Dialog::None) {
        return;
    }

    m_Visible = false;
    m_Opacity = 0.0;
    m_HoverAction = Action::None;
    m_PressedAction = Action::None;
    m_PointerOverInterface = false;
    m_ReleaseForInteraction = false;

    if (m_Dialog != Dialog::None) {
        closeDialog();
    }

    updateCaptureState();
    redraw();
}

void CommandBar::setPinned(bool pinned)
{
    if (m_Pinned == pinned) {
        return;
    }

    m_Pinned = pinned;
    m_PinnedLocked = true;

    StreamingPreferences::get()->commandBarPinned = pinned;
    StreamingPreferences::get()->save();

    if (pinned) {
        reveal();
    }
    else {
        // Let the bar hide again once the pointer leaves it
        m_HideDeadline = SDL_GetTicks() + k_AutoHideDelay;
    }

    redraw();
}

void CommandBar::toggleVisible()
{
    if (!isEnabled() || !isFullScreen()) {
        return;
    }

    if (m_Visible) {
        // A pinned bar is meant to stay on screen; unpin it to hide it
        if (!m_Pinned) {
            hide();
        }
        return;
    }

    reveal();

    // The hotkey is the only way to reach the bar while the mouse is captured,
    // so release the pointer and move the cursor onto the bar. Capture is
    // restored when the pointer leaves the bar again.
    m_ReleaseForInteraction = true;
    updateCaptureState();

    if (!isPointerReleased() && m_Session != nullptr && m_Session->m_InputHandler != nullptr) {
        m_Session->m_InputHandler->setCaptureActive(false);
        SDL_ShowCursor(SDL_ENABLE);
    }

    if (!m_ButtonRects.isEmpty() && m_Window != nullptr) {
        // SDL_WarpMouseInWindow() uses window points, which is also the unit
        // the layout is expressed in.
        const QRectF target = m_ButtonRects.first();
        SDL_WarpMouseInWindow(m_Window,
                              (int)qRound(target.center().x()),
                              (int)qRound(target.center().y()));
    }
}

void CommandBar::updateCaptureState()
{
    if (m_Session == nullptr || m_Session->m_InputHandler == nullptr) {
        return;
    }

    SdlInputHandler* inputHandler = m_Session->m_InputHandler;

    // The pointer only has to be released while the user is actually
    // interacting with the bar. A pinned bar that is merely on screen must not
    // keep the mouse away from the game, which would break play entirely.
    //
    // Interacting means the pointer is over the interface or the bar was opened
    // with the hotkey, since a captured mouse has no cursor to hover with.
    const bool interacting = isFullScreen()
                             && m_Visible
                             && (m_PointerOverInterface || m_ReleaseForInteraction);

    if (interacting) {
        if (!m_CapturedBeforeRelease && inputHandler->isCaptureActive()) {
            m_CapturedBeforeRelease = true;
            inputHandler->setCaptureActive(false);
            SDL_ShowCursor(SDL_ENABLE);
        }
    }
    else if (m_CapturedBeforeRelease) {
        m_CapturedBeforeRelease = false;
        m_ReleaseForInteraction = false;

        inputHandler->setCaptureActive(true);
    }
}

void CommandBar::setPointerOverInterface(bool over)
{
    if (m_PointerOverInterface == over) {
        return;
    }

    m_PointerOverInterface = over;
    updateCaptureState();
}

//------------------------------------------------------------------------------
// Animation
//------------------------------------------------------------------------------

void CommandBar::update()
{
    if (m_Session == nullptr || m_OverlayManager == nullptr) {
        return;
    }

    if (m_Window != m_Session->m_Window) {
        m_Window = m_Session->m_Window;
        layout();
    }

    // The sensitivity hotkeys can change these values behind our back, and the
    // bar shows them, so keep the cached copies in sync.
    StreamingPreferences* prefs = StreamingPreferences::get();
    if (m_PrefsMouseSensitivity != prefs->mouseSensitivity ||
            m_PrefsMouseSensitivityEnabled != prefs->mouseSensitivityEnabled) {
        m_PrefsMouseSensitivity = prefs->mouseSensitivity;
        m_PrefsMouseSensitivityEnabled = prefs->mouseSensitivityEnabled;

        if (m_Visible) {
            redraw();
        }
    }

    // The bar is full-screen only
    if (!isFullScreen()) {
        if (m_Visible) {
            hide();
        }
        return;
    }

    const quint32 now = SDL_GetTicks();

    if (m_Visible && !m_Pinned && m_HideDeadline != 0 && now >= m_HideDeadline) {
        m_HideDeadline = 0;
        hide();
        return;
    }

    if (m_KeyHintsVisible && now >= m_KeyHintsDeadline) {
        hideKeyHints();
    }
}

//------------------------------------------------------------------------------
// Input
//------------------------------------------------------------------------------

void CommandBar::handleKeyEvent(SDL_KeyboardEvent* event)
{
    if (event->state != SDL_PRESSED || event->repeat) {
        return;
    }

    if (event->keysym.sym != SDLK_ESCAPE) {
        return;
    }

    // Escape closes whatever the bar is showing, innermost first
    if (m_Dialog != Dialog::None) {
        closeDialog();
    }
    else if (m_KeyHintsVisible) {
        hideKeyHints();
    }
    else if (m_Visible) {
        hide();
    }
}

void CommandBar::handleMouseMotionEvent(SDL_MouseMotionEvent* event)
{
    if (!isEnabled() || m_OverlayManager == nullptr) {
        return;
    }

    const QPointF pos(event->x, event->y);
    const quint32 now = SDL_GetTicks();
    bool changed = false;

    // Touching the top edge of the window reveals an unpinned bar. This needs
    // a pointer that has a position, which captured relative mode does not
    // provide, so the hotkey is used in that case instead.
    if (!m_Visible && isFullScreen() && isPointerReleased()
            && pos.y() <= k_EdgeThreshold) {
        reveal();
        return;
    }

    if (!m_Visible) {
        return;
    }

    // Hover state for the bar buttons
    Action hoveredAction = Action::None;
    for (int i = 0; i < m_ButtonRects.count(); i++) {
        if (m_ButtonRects[i].contains(pos)) {
            hoveredAction = (Action)i;
            break;
        }
    }

    if (hoveredAction != m_HoverAction) {
        m_HoverAction = hoveredAction;
        m_PressedAction = Action::None;
        changed = true;
    }

    // Hover state for the dialog buttons
    DialogButton hoveredDialogButton = DialogButton::None;
    if (m_Dialog != Dialog::None) {
        const QVector<DialogButton> order = {
            DialogButton::QuitStream, DialogButton::QuitApp, DialogButton::Cancel };

        for (int i = 0; i < order.count() && i < m_DialogButtonRects.count(); i++) {
            if (m_DialogButtonRects[i].contains(pos)) {
                hoveredDialogButton = order[i];
                break;
            }
        }
    }

    if (hoveredDialogButton != m_HoverDialogButton) {
        m_HoverDialogButton = hoveredDialogButton;
        changed = true;
    }

    // Hover state for the shortcut list controls
    HintControl hoveredHintControl = HintControl::None;
    if (m_KeyHintsVisible) {
        if (m_KeyHintsCheckBoxRect.contains(pos)) {
            hoveredHintControl = HintControl::DontShowAgain;
        }
        else if (m_KeyHintsGotItRect.contains(pos)) {
            hoveredHintControl = HintControl::GotIt;
        }
        else if (m_KeyHintsCloseRect.contains(pos)) {
            hoveredHintControl = HintControl::Close;
        }
    }

    if (hoveredHintControl != m_HoverHintControl) {
        m_HoverHintControl = hoveredHintControl;
        changed = true;
    }

    // Keep the bar up while the pointer is over any part of it, and start the
    // auto-hide countdown as soon as it is not
    const bool overBar = hoveredAction != Action::None
                         || m_BarRect.contains(pos)
                         || hoveredDialogButton != DialogButton::None
                         || (m_Dialog != Dialog::None && m_DialogRect.contains(pos))
                         || hoveredHintControl != HintControl::None
                         || (m_KeyHintsVisible && m_KeyHintsRect.contains(pos));

    if (overBar) {
        m_HideDeadline = 0;
    }
    else if (!m_Pinned && m_HideDeadline == 0) {
        m_HideDeadline = now + k_AutoHideDelay;
    }

    // Capture is only released while the pointer is interacting with the bar.
    // The position of a captured mouse is meaningless for hit testing, so the
    // pointer is only tracked once capture has been released (which the hotkey
    // does when it warps the cursor onto the bar).
    setPointerOverInterface(overBar && isPointerReleased());

    if (changed) {
        redraw();
    }
}

void CommandBar::handleMouseButtonEvent(SDL_MouseButtonEvent* event)
{
    if (event->button != SDL_BUTTON_LEFT) {
        return;
    }

    const QPointF pos(event->x, event->y);

    if (event->state == SDL_PRESSED) {
        if (m_Dialog != Dialog::None) {
            m_PressedDialogButton = m_HoverDialogButton;
            redraw();
            return;
        }

        if (m_KeyHintsVisible && m_HoverHintControl != HintControl::None) {
            m_PressedHintControl = m_HoverHintControl;
            redraw();
            return;
        }

        if (m_Visible && m_HoverAction != Action::None) {
            m_PressedAction = m_HoverAction;
            redraw();
        }

        return;
    }

    // Button releases
    if (m_Dialog != Dialog::None) {
        const DialogButton pressed = m_PressedDialogButton;
        m_PressedDialogButton = DialogButton::None;

        if (pressed != DialogButton::None && pressed == m_HoverDialogButton) {
            activateDialogButton(pressed);
        }
        return;
    }

    if (m_PressedHintControl != HintControl::None) {
        const HintControl pressed = m_PressedHintControl;
        m_PressedHintControl = HintControl::None;

        if (pressed == m_HoverHintControl) {
            if (pressed == HintControl::DontShowAgain) {
                // The checkbox only records the choice; it is committed when
                // the user acknowledges the panel
                m_KeyHintsDontShowAgain = !m_KeyHintsDontShowAgain;
                redraw();
                return;
            }

            if (pressed == HintControl::GotIt) {
                StreamingPreferences::get()->showKeyHintsOnStreamStart = !m_KeyHintsDontShowAgain;
                StreamingPreferences::get()->save();
                m_PrefsShowKeyHintsOnStreamStart = !m_KeyHintsDontShowAgain;
            }

            hideKeyHints();
        }
        return;
    }

    if (m_Visible) {
        const Action pressed = m_PressedAction;
        m_PressedAction = Action::None;

        if (pressed != Action::None && pressed == m_HoverAction) {
            activate(pressed);
        }
        else if (m_HoverAction == Action::None && !m_Pinned
                 && !m_BarRect.adjusted(-6, -6, 6, 6).contains(pos)) {
            // Clicking on the video dismisses an unpinned bar
            hide();
        }
    }
}

void CommandBar::handleMouseWheelEvent(SDL_MouseWheelEvent* event)
{
    if (!m_Visible || m_HoverAction != Action::MouseSensitivity) {
        return;
    }

    int direction = event->y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (direction == 0 && event->preciseY != 0.0f) {
        direction = event->preciseY > 0.0f ? 1 : -1;
    }
#endif

    if (direction != 0) {
        adjustMouseSensitivity(direction);
    }
}

//------------------------------------------------------------------------------
// Actions
//------------------------------------------------------------------------------

void CommandBar::activate(Action action)
{
    switch (action) {
    case Action::Pin:
        setPinned(!m_Pinned);
        break;

    case Action::Windowed:
        setWindowed();
        break;

    case Action::Minimize:
        minimizeWindow();
        break;

    case Action::MouseSensitivity:
        // Clicking toggles the adjustment; the wheel changes the value
        toggleMouseSensitivityAdjustment();
        break;

    case Action::KeyHints:
        if (m_KeyHintsVisible) {
            hideKeyHints();
        }
        else {
            showKeyHints();
        }
        break;

    case Action::QuitStream:
        openQuitDialog();
        break;

    default:
        break;
    }
}

void CommandBar::setWindowed()
{
    if (m_Session == nullptr) {
        return;
    }

    // Leaving full-screen hides the bar, which also releases the pointer so
    // the user can work with the window frame
    hide();
    m_Session->toggleFullscreen();
}

void CommandBar::minimizeWindow()
{
    if (m_Window == nullptr) {
        return;
    }

    hide();
    SDL_MinimizeWindow(m_Window);
}

void CommandBar::adjustMouseSensitivity(int stepDirection)
{
    StreamingPreferences* prefs = StreamingPreferences::get();

    if (!prefs->mouseSensitivityEnabled) {
        return;
    }

    const int newValue = StreamingPreferences::clampMouseSensitivity(
                StreamingPreferences::stepMouseSensitivity(prefs->mouseSensitivity, stepDirection));

    if (newValue == prefs->mouseSensitivity) {
        // Already at the end of the range; still show the current value
        showSensitivityStatus();
        return;
    }

    prefs->mouseSensitivity = newValue;
    prefs->save();

    m_PrefsMouseSensitivity = newValue;

    if (m_Session != nullptr && m_Session->m_InputHandler != nullptr) {
        m_Session->m_InputHandler->applyMouseSensitivity();
    }

    showSensitivityStatus();

    // Refresh the tooltip, since it shows the current value
    redraw();
}

void CommandBar::toggleMouseSensitivityAdjustment()
{
    StreamingPreferences* prefs = StreamingPreferences::get();

    prefs->mouseSensitivityEnabled = !prefs->mouseSensitivityEnabled;
    prefs->save();

    m_PrefsMouseSensitivityEnabled = prefs->mouseSensitivityEnabled;

    if (m_Session != nullptr && m_Session->m_InputHandler != nullptr) {
        m_Session->m_InputHandler->applyMouseSensitivity();
    }

    showSensitivityStatus();
    redraw();
}

void CommandBar::showSensitivityStatus()
{
    if (m_OverlayManager == nullptr) {
        return;
    }

    const QString text = m_PrefsMouseSensitivityEnabled
            ? tr("Mouse sensitivity: %1%").arg(m_PrefsMouseSensitivity)
            : tr("Mouse sensitivity adjustment: off");

    // Reuse the status overlay that the sensitivity hotkeys also use
    m_OverlayManager->updateOverlayText(Overlay::OverlayStatusUpdate, text.toUtf8().constData());
    m_OverlayManager->setOverlayState(Overlay::OverlayStatusUpdate, true);

    if (m_StatusOverlayTimer != 0) {
        SDL_RemoveTimer(m_StatusOverlayTimer);
    }

    m_StatusOverlayTimer = SDL_AddTimer(k_StatusDuration, hideStatusOverlayTimerCallback, this);
}

Uint32 CommandBar::hideStatusOverlayTimerCallback(Uint32, void* param)
{
    CommandBar* bar = static_cast<CommandBar*>(param);

    if (s_CommandBar == bar) {
        bar->m_StatusOverlayTimer = 0;

        if (bar->m_OverlayManager != nullptr) {
            bar->m_OverlayManager->setOverlayState(Overlay::OverlayStatusUpdate, false);
        }
    }

    // One-shot timer
    return 0;
}

void CommandBar::openQuitDialog()
{
    m_Dialog = Dialog::QuitStream;
    m_HoverDialogButton = DialogButton::None;
    m_PressedDialogButton = DialogButton::None;

    layout();
    redraw();
}

void CommandBar::closeDialog()
{
    m_Dialog = Dialog::None;
    m_HoverDialogButton = DialogButton::None;
    m_PressedDialogButton = DialogButton::None;

    if (m_OverlayManager != nullptr) {
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBarDialog, false);
    }
}

void CommandBar::activateDialogButton(DialogButton button)
{
    Session* session = m_Session;

    closeDialog();
    redraw();

    if (session == nullptr) {
        return;
    }

    switch (button) {
    case DialogButton::QuitStream:
        // End the stream and return to the Moonlight UI
        session->setShouldExit(false);
        break;

    case DialogButton::QuitApp:
        // End the stream and close Moonlight
        session->setShouldExit(true);
        break;

    case DialogButton::Cancel:
    default:
        return;
    }

    // The session's event loop turns this into a clean shutdown
    SDL_Event event = {};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

//------------------------------------------------------------------------------
// Keyboard shortcut list
//------------------------------------------------------------------------------

void CommandBar::showKeyHints()
{
    if (m_OverlayManager == nullptr || !isFullScreen()) {
        return;
    }

    m_KeyHintsVisible = true;
    m_KeyHintsDeadline = SDL_GetTicks() + k_KeyHintsDuration;
    m_KeyHintsDontShowAgain = false;
    m_HoverHintControl = HintControl::None;
    m_PressedHintControl = HintControl::None;

    layout();
    redraw();
}

void CommandBar::hideKeyHints()
{
    if (!m_KeyHintsVisible) {
        return;
    }

    m_KeyHintsVisible = false;
    m_HoverHintControl = HintControl::None;
    m_PressedHintControl = HintControl::None;

    if (m_OverlayManager != nullptr) {
        m_OverlayManager->setOverlayState(Overlay::OverlayKeyHints, false);
    }
}

//------------------------------------------------------------------------------
// Painting
//------------------------------------------------------------------------------

QImage CommandBar::createOverlayImage(qreal logicalWidth, qreal logicalHeight) const
{
    const int width = qMax(1, (int)qCeil(logicalWidth * m_UiScale));
    const int height = qMax(1, (int)qCeil(logicalHeight * m_UiScale));

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    return image;
}

void CommandBar::beginPaint(QPainter& painter) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Layout is in logical pixels, so map them onto the physical pixels of the
    // overlay surface
    painter.scale(m_UiScale, m_UiScale);

    // Everything fades with the bar
    painter.setOpacity(qBound(0.0, m_Opacity, 1.0));
}

void CommandBar::stageOverlay(Overlay::OverlayType type, const QImage& image, const QPointF& logicalPos)
{
    if (m_OverlayManager == nullptr) {
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                          image.width(),
                                                          image.height(),
                                                          32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to allocate command bar surface: %s",
                     SDL_GetError());
        return;
    }

    // SDL_ConvertPixels handles the channel order expected by the renderers,
    // so the image and the surface can use different pixel formats safely.
    if (SDL_ConvertPixels(image.width(), image.height(),
                          SDL_PIXELFORMAT_ARGB8888, image.constBits(), image.bytesPerLine(),
                          surface->format->format, surface->pixels, surface->pitch) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to convert command bar overlay: %s",
                     SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    // Command bar overlays are anchored to the top left corner of the window,
    // so their offsets are plain window coordinates
    m_OverlayManager->setOverlayAnchor(type, Overlay::OverlayAnchor::AnchorTopLeft,
                                       (int)qRound(logicalPos.x() * m_UiScale),
                                       (int)qRound(logicalPos.y() * m_UiScale));

    m_OverlayManager->updateOverlaySurface(type, surface);
    m_OverlayManager->setOverlayState(type, true);
}

void CommandBar::redraw()
{
    if (m_OverlayManager == nullptr) {
        return;
    }

    layout();

    if (m_Visible && m_Opacity > 0.0 && isFullScreen()) {        paintBar();
    }
    else {
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBar, false);
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBarTooltip, false);
    }

    if (m_Dialog != Dialog::None) {
        paintDialog();
    }

    if (m_KeyHintsVisible) {
        paintKeyHints();
    }
}

void CommandBar::paintBar()
{
    const QRectF panel(k_PanelMargin, k_PanelMargin, m_BarRect.width(), m_BarRect.height());

    QImage image = createOverlayImage(m_BarRect.width() + k_PanelMargin * 2,
                                      m_BarRect.height() + k_PanelMargin * 2);

    {
        QPainter painter(&image);
        beginPaint(painter);

        paintPanel(painter, panel, k_BarCornerRadius, false);

        const QVector<ButtonSpec> specs = buildButtonSpecs();

        for (int i = 0; i < specs.count(); i++) {
            const Action action = (Action)i;
            const QRectF button = m_ButtonRects[i].translated(k_PanelMargin, k_PanelMargin);

            QRgb background = 0;
            if (m_PressedAction == action) {
                background = k_ColorButtonPressed;
            }
            else if (m_HoverAction == action) {
                background = k_ColorButtonHover;
            }
            else if (specs[i].toggled) {
                background = k_ColorButtonActive;
            }

            if (background != 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(color(background));
                painter.drawRoundedRect(button, k_ButtonRadius, k_ButtonRadius);
            }

            const bool inactiveSensitivity = action == Action::MouseSensitivity
                                             && !m_PrefsMouseSensitivityEnabled;

            const QRectF iconBox(button.center().x() - 10, button.center().y() - 10, 20, 20);
            paintIcon(painter, iconBox, iconForAction(action),
                      color(inactiveSensitivity ? k_ColorIconInactive : k_ColorIcon));
        }

        if (!m_SeparatorRect.isNull()) {
            const QRectF separator = m_SeparatorRect.translated(k_PanelMargin, k_PanelMargin);
            painter.setPen(QPen(color(k_ColorSeparator), 1.0));
            painter.drawLine(separator.topLeft(), separator.bottomLeft());
        }
    }

    stageOverlay(Overlay::OverlayCommandBar, image,
                 QPointF(m_BarRect.left() - k_PanelMargin, m_BarRect.top() - k_PanelMargin));

    if (m_HoverAction != Action::None && m_Dialog == Dialog::None) {
        paintTooltip();
    }
    else {
        m_OverlayManager->setOverlayState(Overlay::OverlayCommandBarTooltip, false);
    }
}

void CommandBar::paintTooltip()
{
    const int index = (int)m_HoverAction;
    if (index < 0 || index >= m_ButtonRects.count()) {
        return;
    }

    const ButtonSpec spec = buildButtonSpecs()[index];

    const QFont tooltipFont = uiFont(k_TooltipFontSize);
    const QFont shortcutFont = uiFont(k_TooltipShortcutFontSize, true);
    const QFontMetricsF tooltipMetrics(tooltipFont);
    const QFontMetricsF shortcutMetrics(shortcutFont);

    const qreal padding = 8;
    const qreal textWidth = qMax(tooltipMetrics.horizontalAdvance(spec.tooltip),
                                 spec.hasShortcut
                                     ? shortcutMetrics.horizontalAdvance(spec.shortcut)
                                     : 0.0);
    const qreal panelWidth = qMax(90.0, textWidth + padding * 2);
    const qreal panelHeight = padding * 2 + tooltipMetrics.height()
                              + (spec.hasShortcut ? shortcutMetrics.height() + 2 : 0);

    QImage image = createOverlayImage(panelWidth + k_PanelMargin * 2,
                                      panelHeight + k_PanelMargin * 2);

    {
        QPainter painter(&image);
        beginPaint(painter);

        const QRectF panel(k_PanelMargin, k_PanelMargin, panelWidth, panelHeight);
        paintPanel(painter, panel, 6, false);

        const qreal textLeft = k_PanelMargin + padding;
        const qreal textMaxWidth = panelWidth - padding * 2;
        qreal y = k_PanelMargin + padding;

        painter.setFont(tooltipFont);
        painter.setPen(color(k_ColorText));
        painter.drawText(QRectF(textLeft, y, textMaxWidth, tooltipMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         tooltipMetrics.elidedText(spec.tooltip, Qt::ElideRight, textMaxWidth));

        y += tooltipMetrics.height();

        if (spec.hasShortcut) {
            painter.setFont(shortcutFont);
            painter.setPen(color(k_ColorShortcut));
            painter.drawText(QRectF(textLeft, y + 2, textMaxWidth, shortcutMetrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, spec.shortcut);
        }
    }

    const qreal logicalWidth = m_WindowWidth;
    const qreal logicalHeight = m_WindowHeight;

    // Center the tooltip over its button, keeping it inside the window
    qreal x = m_ButtonRects[index].center().x() - panelWidth / 2.0;
    x = qBound((qreal)0.0, x, qMax((qreal)0.0, logicalWidth - panelWidth));

    qreal y = m_BarRect.bottom() + k_TooltipGap;
    if (y + panelHeight > logicalHeight) {
        // No room below the bar, so place the tooltip above it
        y = qMax((qreal)0.0, m_BarRect.top() - panelHeight - k_TooltipGap);
    }

    m_TooltipRect = QRectF(x, y, panelWidth, panelHeight);

    stageOverlay(Overlay::OverlayCommandBarTooltip, image, m_TooltipRect.topLeft());
}

void CommandBar::paintDialog()
{
    const qreal padding = 18;
    const qreal buttonHeight = 26;
    const qreal buttonSpacing = 8;

    const QString title = tr("Quit stream");
    const QString message = tr("Do you want to return to Moonlight, or close Moonlight too?");

    const QFont titleFont = uiFont(k_DialogTitleFontSize, true);
    const QFont messageFont = uiFont(k_DialogFontSize);
    const QFont buttonFont = uiFont(k_DialogFontSize);

    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF messageMetrics(messageFont);
    const QFontMetricsF buttonMetrics(buttonFont);

    const QStringList labels = {
        tr("Return to Moonlight"),
        tr("Quit Moonlight"),
        tr("Cancel"),
    };

    // Keep the dialog narrow enough to stay inside the window at any size
    const qreal logicalWidth = m_WindowWidth;
    const qreal logicalHeight = m_WindowHeight;
    const qreal maxWidth = qMax(240.0, logicalWidth - 60);

    const qreal maxTextWidth = maxWidth - padding * 2;
    const qreal messageWidth = qMin(messageMetrics.horizontalAdvance(message), maxTextWidth);

    qreal buttonsWidth = buttonSpacing * (labels.count() - 1);
    for (const QString& label : labels) {
        buttonsWidth += buttonMetrics.horizontalAdvance(label) + 28;
    }

    const qreal panelWidth = qMax(220.0,
                                  qMin(maxWidth, qMax(qMax(titleMetrics.horizontalAdvance(title),
                                                           messageWidth) + padding * 2,
                                                      qMin(buttonsWidth + padding * 2, maxWidth))));
    const qreal messageHeight = messageMetrics.boundingRect(
                QRectF(0, 0, panelWidth - padding * 2, 1000),
                Qt::TextWordWrap, message).height();
    const qreal panelHeight = padding * 2 + titleMetrics.height() + 6
                              + messageHeight + 16 + buttonHeight;

    // The dialog is centered in the window
    const QRectF panel((logicalWidth - panelWidth) / 2.0,
                       (logicalHeight - panelHeight) / 2.0,
                       panelWidth, panelHeight);

    // The dialog overlay covers the whole window so it can dim the video
    QImage image = createOverlayImage(logicalWidth, logicalHeight);

    m_DialogButtonRects.clear();

    {
        QPainter painter(&image);
        beginPaint(painter);

        painter.fillRect(QRectF(0, 0, logicalWidth, logicalHeight), color(k_ColorDialogDim));

        paintPanel(painter, panel, 8, true);

        qreal y = panel.top() + padding;

        painter.setFont(titleFont);
        painter.setPen(color(k_ColorText));
        painter.drawText(QRectF(panel.left() + padding, y,
                                panelWidth - padding * 2, titleMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, title);

        y += titleMetrics.height() + 6;

        painter.setFont(messageFont);
        painter.setPen(color(k_ColorTextDim));
        painter.drawText(QRectF(panel.left() + padding, y, panelWidth - padding * 2, messageHeight),
                         Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, message);

        y += messageHeight + 16;

        qreal x = panel.left() + padding;

        for (int i = 0; i < labels.count(); i++) {
            const qreal width = buttonMetrics.horizontalAdvance(labels[i]) + 28;
            const QRectF button(x, y, width, buttonHeight);

            const DialogButton dialogButton = i == 0 ? DialogButton::QuitStream
                                            : i == 1 ? DialogButton::QuitApp
                                                     : DialogButton::Cancel;

            QRgb background = k_ColorNeutralButton;

            if (m_HoverDialogButton == dialogButton) {
                background = dialogButton == DialogButton::QuitApp
                        ? k_ColorDangerHover : k_ColorNeutralButtonHover;
            }
            else if (dialogButton == DialogButton::QuitStream) {
                background = k_ColorAccent;
            }
            else if (dialogButton == DialogButton::QuitApp) {
                background = k_ColorDanger;
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(color(background));
            painter.drawRoundedRect(button, 4, 4);

            painter.setFont(buttonFont);
            painter.setPen(color(k_ColorText));
            painter.drawText(button, Qt::AlignCenter, labels[i]);

            m_DialogButtonRects.append(button);
            x += width + buttonSpacing;
        }
    }

    m_DialogRect = panel;

    stageOverlay(Overlay::OverlayCommandBarDialog, image, QPointF(0, 0));
}

void CommandBar::paintKeyHints()
{
    const qreal padding = 16;
    const qreal rowHeight = uiFont(k_HintFontSize).pointSizeF() * 2.0;
    const qreal columnGap = 18;
    const qreal footerGap = 14;
    const qreal closeSize = 16;
    const qreal checkboxSize = 13;

    const QString title = tr("Keyboard shortcuts");
    const QString checkboxLabel = tr("Do not show this again");
    const QString gotItLabel = tr("Got it");

    const QFont titleFont = uiFont(k_HintTitleFontSize, true);
    const QFont hintFont = uiFont(k_HintFontSize);
    const QFont shortcutFont = uiFont(k_HintShortcutFontSize, true);
    const QFont footerFont = uiFont(k_HintFontSize);

    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF hintMetrics(hintFont);
    const QFontMetricsF shortcutMetrics(shortcutFont);
    const QFontMetricsF footerMetrics(footerFont);

    const QStringList lines = buildKeyHintLines();

    QStringList shortcuts;
    QStringList descriptions;
    qreal shortcutColumnWidth = 0;
    qreal descriptionColumnWidth = 0;

    for (const QString& line : lines) {
        const int split = line.indexOf(QStringLiteral("  "));
        const QString shortcut = split >= 0 ? line.left(split) : QString();
        const QString description = split >= 0 ? line.mid(split + 2) : line;

        shortcuts.append(shortcut);
        descriptions.append(description);

        shortcutColumnWidth = qMax(shortcutColumnWidth, shortcutMetrics.horizontalAdvance(shortcut));
        descriptionColumnWidth = qMax(descriptionColumnWidth, hintMetrics.horizontalAdvance(description));
    }

    const qreal lineHeight = hintMetrics.height() + 6;
    const qreal gotItWidth = footerMetrics.horizontalAdvance(gotItLabel) + 28;

    const qreal contentWidth = shortcutColumnWidth + columnGap + descriptionColumnWidth;
    const qreal footerWidth = checkboxSize + 7 + footerMetrics.horizontalAdvance(checkboxLabel) + 20 + gotItWidth;
    const qreal titleWidth = titleMetrics.horizontalAdvance(title) + closeSize + 8;

    const qreal panelWidth = qMax(qMax(contentWidth, footerWidth), titleWidth) + padding * 2;
    const qreal panelHeight = padding * 2 + titleMetrics.height() + 12
                              + lines.count() * lineHeight
                              + footerGap + footerMetrics.height() + 8;

    const qreal logicalWidth = m_WindowWidth;
    const qreal logicalHeight = m_WindowHeight;

    // Keep the panel inside the window, and away from the bar
    const qreal panelX = qMax((qreal)0.0, (logicalWidth - panelWidth) / 2.0);
    const qreal panelY = qMin(qMax(m_BarRect.bottom() + 24, logicalHeight / 12.0),
                              qMax((qreal)0.0, logicalHeight - panelHeight - 24));

    QImage image = createOverlayImage(panelWidth + k_PanelMargin * 2,
                                      panelHeight + k_PanelMargin * 2);

    {
        QPainter painter(&image);
        beginPaint(painter);

        const QRectF panel(k_PanelMargin, k_PanelMargin, panelWidth, panelHeight);
        paintPanel(painter, panel, 8, true);

        const qreal left = panel.left() + padding;
        qreal y = panel.top() + padding;

        painter.setFont(titleFont);
        painter.setPen(color(k_ColorText));
        painter.drawText(QRectF(left, y, panelWidth - padding * 2 - closeSize, titleMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, title);

        // Close button in the top right corner
        m_KeyHintsCloseRect = QRectF(panel.right() - padding - closeSize,
                                     y + (titleMetrics.height() - closeSize) / 2.0,
                                     closeSize, closeSize);
        painter.setPen(QPen(color(m_HoverHintControl == HintControl::Close
                                  ? k_ColorText : k_ColorTextDim), 1.6));
        painter.drawLine(m_KeyHintsCloseRect.topLeft() + QPointF(4.5, 4.5),
                         m_KeyHintsCloseRect.bottomRight() - QPointF(4.5, 4.5));
        painter.drawLine(m_KeyHintsCloseRect.topRight() + QPointF(-4.5, 4.5),
                         m_KeyHintsCloseRect.bottomLeft() + QPointF(4.5, -4.5));

        y += titleMetrics.height() + 12;

        for (int i = 0; i < lines.count(); i++) {
            painter.setFont(shortcutFont);
            painter.setPen(color(k_ColorShortcut));
            painter.drawText(QRectF(left, y, shortcutColumnWidth, hintMetrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, shortcuts[i]);

            painter.setFont(hintFont);
            painter.setPen(color(k_ColorText));
            painter.drawText(QRectF(left + shortcutColumnWidth + columnGap, y,
                                    descriptionColumnWidth, hintMetrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, descriptions[i]);

            y += lineHeight;
        }

        y += footerGap - 6;

        // "Do not show this again" checkbox
        m_KeyHintsCheckBoxRect = QRectF(left, y + (footerMetrics.height() - checkboxSize) / 2.0,
                                        checkboxSize, checkboxSize);

        painter.setPen(QPen(color(k_ColorCheckBox), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(m_KeyHintsCheckBoxRect, 3, 3);

        if (m_KeyHintsDontShowAgain) {
            QPen checkPen(color(k_ColorAccent), 2.0);
            checkPen.setCapStyle(Qt::RoundCap);
            painter.setPen(checkPen);
            painter.drawLine(m_KeyHintsCheckBoxRect.topLeft() + QPointF(2.5, 6.5),
                             m_KeyHintsCheckBoxRect.topLeft() + QPointF(5.0, 9.5));
            painter.drawLine(m_KeyHintsCheckBoxRect.topLeft() + QPointF(5.0, 9.5),
                             m_KeyHintsCheckBoxRect.topLeft() + QPointF(10.5, 3.5));
        }

        painter.setFont(footerFont);
        painter.setPen(color(m_HoverHintControl == HintControl::DontShowAgain
                             ? k_ColorText : k_ColorTextDim));

        const QRectF checkboxTextRect(m_KeyHintsCheckBoxRect.right() + 7, y,
                                      footerMetrics.horizontalAdvance(checkboxLabel),
                                      footerMetrics.height());
        painter.drawText(checkboxTextRect, Qt::AlignLeft | Qt::AlignVCenter, checkboxLabel);

        // The label is part of the clickable area
        m_KeyHintsCheckBoxRect = m_KeyHintsCheckBoxRect.united(checkboxTextRect);

        // "Got it" button
        m_KeyHintsGotItRect = QRectF(panel.right() - padding - gotItWidth, y - 2,
                                     gotItWidth, footerMetrics.height() + 4);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color(m_HoverHintControl == HintControl::GotIt
                               ? k_ColorButtonHover : k_ColorAccent));
        painter.drawRoundedRect(m_KeyHintsGotItRect, 4, 4);

        painter.setFont(footerFont);
        painter.setPen(color(k_ColorText));
        painter.drawText(m_KeyHintsGotItRect, Qt::AlignCenter, gotItLabel);
    }

    m_KeyHintsRect = QRectF(panelX, panelY, panelWidth, panelHeight);

    stageOverlay(Overlay::OverlayKeyHints, image, m_KeyHintsRect.topLeft());
}
