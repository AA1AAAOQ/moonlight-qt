#pragma once

#include <QString>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    // Performance statistics overlay (top left)
    OverlayDebug,

    // Transient status text such as the current mouse sensitivity (bottom left)
    OverlayStatusUpdate,

    // VMware-style command bar shown while streaming (top center)
    OverlayCommandBar,

    // Tooltip describing the command bar button under the cursor
    OverlayCommandBarTooltip,

    // Confirmation dialog opened from the command bar (centered)
    OverlayCommandBarDialog,

    // Keyboard shortcut hints shown when a stream starts (top center)
    OverlayKeyHints,

    OverlayMax
};

/*
 * Where an overlay is placed inside the window.
 *
 * AnchorTopLeft is used for every overlay that computes its own absolute
 * position (the whole command bar UI), while AnchorBottomLeft is used for
 * overlays whose height is only known once they have been rendered.
 */
enum OverlayAnchor {
    AnchorTopLeft,
    AnchorBottomLeft,
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);

    /*
     * Stages a fully rendered surface for the given overlay and takes ownership
     * of it. The renderer converts the surface into a texture on its next
     * notifyOverlayUpdated() call, so the caller is free to draw the overlay
     * with Qt (which supports fonts the streaming overlay font cannot render).
     */
    void updateOverlaySurface(OverlayType type, SDL_Surface* surface);

    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    /*
     * Sets where an overlay is anchored. The offsets are in window pixels and
     * are applied relative to the anchor, which lets the command bar place its
     * overlays exactly where it wants them.
     */
    void setOverlayAnchor(OverlayType type, OverlayAnchor anchor, int xOffset, int yOffset);

    /*
     * Computes the rectangle an overlay should be drawn at, in window
     * coordinates with the origin in the top-left corner. Renderers call this
     * with the size of the surface they are about to upload plus the size of
     * the window they are rendering into.
     */
    SDL_Rect getOverlayRegion(OverlayType type, int surfaceWidth, int surfaceHeight,
                              int windowWidth, int windowHeight);

    void setOverlayRenderer(IOverlayRenderer* renderer);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth);

    struct {
        bool enabled;
        int fontSize;
        SDL_Color color;
        char text[1024];

        OverlayAnchor anchor;
        int xOffset;
        int yOffset;

        TTF_Font* font;
        SDL_Surface* surface;
    } m_Overlays[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
};

}
