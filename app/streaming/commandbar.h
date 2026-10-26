#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "SDL_compat.h"

#include "streaming/video/overlaymanager.h"

class QPainter;
class QQuickWindow;
class Session;

/*
 * CommandBar implements a VMware-style command bar for a running stream.
 *
 * The bar can only be drawn on top of video frames, so it uses Moonlight's
 * existing overlay pipeline: its whole user interface (bar, tooltip,
 * confirmation dialog and shortcut list) is painted with QPainter into an
 * ARGB image, handed to the overlay manager as an SDL_Surface, and composited
 * over the video by whichever renderer is in use.
 *
 * Painting with Qt instead of the bundled arcade overlay font is what lets the
 * tooltips be translated and renders localized text with a system font.
 *
 * Behavior:
 *  - The bar belongs to full-screen mode. Entering windowed mode hides it, and
 *    returning to full-screen brings a pinned bar back; an unpinned bar waits
 *    for the pointer or the hotkey again.
 *  - A pinned bar stays on screen. An unpinned bar appears when the pointer
 *    touches the top edge of the window and fades out once the pointer leaves
 *    it. A captured mouse has no position inside the window, so
 *    Ctrl+Alt+Shift+B is used in that case.
 *  - The pointer is released while the bar is on screen so its buttons can be
 *    clicked, and the previous capture state is restored once it hides.
 */
class CommandBar : public QObject
{
    Q_OBJECT

public:
    explicit CommandBar(QQuickWindow* qtWindow);
    ~CommandBar() override;

    // Creates (or replaces) the bar used by the active session
    static CommandBar* create(QQuickWindow* qtWindow);
    static CommandBar* get() { return s_CommandBar; }

    // True if the command bar is enabled in the preferences
    static bool isEnabled();

    void notifyStreamStarting();
    void notifyWindowChanged();

    // Drives the fade animation and the auto-hide timers
    void update();

    // Hotkey entry points
    void toggleVisible();
    void showKeyHints();
    void hideKeyHints();

    // Session event dispatch
    void handleKeyEvent(SDL_KeyboardEvent* event);
    void handleMouseMotionEvent(SDL_MouseMotionEvent* event);
    void handleMouseButtonEvent(SDL_MouseButtonEvent* event);
    void handleMouseWheelEvent(SDL_MouseWheelEvent* event);

private:
    enum class Action {
        Pin = 0,
        Windowed,
        Minimize,
        MouseSensitivity,
        KeyHints,
        QuitStream,
        Count,
        None = -1,
    };

    enum class Dialog {
        None,
        QuitStream
    };

    enum class DialogButton {
        None,
        QuitStream,
        QuitApp,
        Cancel
    };

    enum class HintControl {
        None,
        DontShowAgain,
        GotIt,
        Close
    };

    struct ButtonSpec {
        Action action;
        QString tooltip;
        QString shortcut;
        bool hasShortcut;
        bool toggled;
    };

    // Geometry, computed in logical pixels so the layout is independent of the
    // display's device pixel ratio
    void layout();
    void resetGeometry();

    QVector<ButtonSpec> buildButtonSpecs() const;
    int iconForAction(Action action) const;
    QStringList buildKeyHintLines() const;

    // Painting
    void redraw();
    void paintBar();
    void paintTooltip();
    void paintDialog();
    void paintKeyHints();

    QImage createOverlayImage(qreal logicalWidth, qreal logicalHeight) const;
    void beginPaint(QPainter& painter) const;
    void stageOverlay(Overlay::OverlayType type, const QImage& image, const QPointF& logicalPos);

    // State
    bool isFullScreen() const;
    bool isPointerReleased() const;
    void reveal();
    void hide();
    void setPinned(bool pinned);
    void updateCaptureState();
    void setPointerOverInterface(bool over);
    void reloadPreferences();

    // Actions
    void activate(Action action);
    void setWindowed();
    void minimizeWindow();
    void adjustMouseSensitivity(int stepDirection);
    void toggleMouseSensitivityAdjustment();
    void showSensitivityStatus();
    void openQuitDialog();
    void closeDialog();
    void activateDialogButton(DialogButton button);

    static Uint32 hideStatusOverlayTimerCallback(Uint32 interval, void* param);

    QPointer<QQuickWindow> m_QtWindow;
    Session* m_Session;
    Overlay::OverlayManager* m_OverlayManager;
    SDL_Window* m_Window;

    // Window metrics in physical pixels, as reported by SDL
    int m_WindowWidth;
    int m_WindowHeight;
    qreal m_UiScale;

    // Visibility state
    bool m_Visible;
    qreal m_Opacity;
    bool m_Pinned;
    bool m_PinnedLocked;
    quint32 m_HideDeadline;
    bool m_CapturedBeforeRelease;
    bool m_ReleaseForInteraction;
    bool m_PointerOverInterface;

    // Hover and press tracking
    Action m_HoverAction;
    Action m_PressedAction;
    Dialog m_Dialog;
    DialogButton m_HoverDialogButton;
    DialogButton m_PressedDialogButton;
    bool m_KeyHintsVisible;
    quint32 m_KeyHintsDeadline;
    bool m_KeyHintsDontShowAgain;
    HintControl m_HoverHintControl;
    HintControl m_PressedHintControl;

    // Layout of the current frame, in logical pixels
    QRectF m_BarRect;
    QVector<QRectF> m_ButtonRects;
    QRectF m_SeparatorRect;
    QRectF m_TooltipRect;
    QRectF m_DialogRect;
    QVector<QRectF> m_DialogButtonRects;
    QRectF m_KeyHintsRect;
    QRectF m_KeyHintsCheckBoxRect;
    QRectF m_KeyHintsCloseRect;
    QRectF m_KeyHintsGotItRect;

    // Cached preferences
    bool m_PrefsShowKeyHintsOnStreamStart;
    bool m_PrefsMouseSensitivityEnabled;
    int m_PrefsMouseSensitivity;

    SDL_TimerID m_StatusOverlayTimer;

    static CommandBar* s_CommandBar;
};
