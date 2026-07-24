#include "CHyprBorderDecoration.hpp"
#include "../../Compositor.hpp"
#include "../../config/ConfigValue.hpp"
#include "../../managers/fullscreen/FullscreenController.hpp"
#include "../pass/RectPassElement.hpp"
#include "../Renderer.hpp"
#include "../../layout/space/Space.hpp"
#include "../../desktop/Workspace.hpp"
#include "../../state/MonitorState.hpp"

CHyprBorderDecoration::CHyprBorderDecoration(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    ;
}

SDecorationPositioningInfo CHyprBorderDecoration::getPositioningInfo() {
    const auto BORDERSIZE = m_window->getRealBorderSize();
    m_extents             = {{BORDERSIZE, BORDERSIZE}, {BORDERSIZE, BORDERSIZE}};

    if (doesntWantBorders())
        m_extents = {{}, {}};

    // When a group bar is present, the top border is never shown and must not
    // take up layout space – the group bar replaces it entirely.
    if (m_window->getDecorationByType(DECORATION_GROUPBAR))
        m_extents.topLeft.y = 0;

    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_STICKY;
    info.desiredExtents = m_extents;
    info.reserved       = true;
    info.edges          = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;

    m_reportedExtents = m_extents;
    return info;
}

void CHyprBorderDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assignedGeometry = reply.assignedGeometry;
}

CBox CHyprBorderDecoration::assignedBoxGlobal() {
    CBox box = m_assignedGeometry;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, m_window));

    const auto PWORKSPACE = m_window->m_workspace;

    if (!PWORKSPACE)
        return box;

    const auto WORKSPACEOFFSET = PWORKSPACE && !m_window->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();
    return box.translate(WORKSPACEOFFSET);
}

void CHyprBorderDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    if (doesntWantBorders())
        return;

    if (m_assignedGeometry.width < m_extents.topLeft.x + 1 || m_assignedGeometry.height < m_extents.topLeft.y + 1)
        return;

    const int borderSize = m_window->getRealBorderSize();

    CBox windowBox = assignedBoxGlobal()
                         .translate(-pMonitor->m_position + m_window->m_floatingOffset)
                         .expand(-borderSize)
                         .scale(pMonitor->m_scale)
                         .round();

    if (windowBox.width < 1 || windowBox.height < 1)
        return;

    const auto borderCol  = m_window->m_realBorderColor;
    const auto color      = borderCol.m_colors.empty() ? CHyprColor{0} : borderCol.m_colors[0];
    const int  bs         = std::round(borderSize * pMonitor->m_scale);

    // Determine which edges should be suppressed:
    // - tiled windows touching a screen edge
    // - top edge when a group bar is present (regardless of position)
    // Floating windows keep borders on all four sides.
    bool edgeL = false, edgeR = false, edgeT = false, edgeB = false;
    if (!m_window->m_isFloating) {
        if (const auto PWS = m_window->m_workspace; PWS && PWS->m_space) {
            const auto SURFACE = m_window->getWindowMainSurfaceBox();
            const auto WORK    = PWS->m_space->workArea();
            edgeL = STICKS(SURFACE.x, WORK.x);
            edgeR = STICKS(SURFACE.x + SURFACE.w, WORK.x + WORK.w);
            edgeT = STICKS(SURFACE.y, WORK.y);
            edgeB = STICKS(SURFACE.y + SURFACE.h, WORK.y + WORK.h);
        }
        // Group bar replaces the top border entirely.
        if (m_window->getDecorationByType(DECORATION_GROUPBAR))
            edgeT = true;
    }

    // Draw each visible side as a rectangle.  Vertical sides extend one
    // borderSize past the top / bottom edges and horizontal sides extend
    // one borderSize past the left / right edges so that corners are
    // continuous (opaque, no rounding).
    if (!edgeL) {
        CRectPassElement::SRectData r;
        r.box   = {windowBox.x - bs, windowBox.y - bs, bs, windowBox.h + 2 * bs};
        r.color = color;
        g_pHyprRenderer->addPassElement(makeUnique<CRectPassElement>(r));
    }
    if (!edgeR) {
        CRectPassElement::SRectData r;
        r.box   = {windowBox.x + windowBox.w, windowBox.y - bs, bs, windowBox.h + 2 * bs};
        r.color = color;
        g_pHyprRenderer->addPassElement(makeUnique<CRectPassElement>(r));
    }
    if (!edgeT) {
        CRectPassElement::SRectData r;
        r.box   = {windowBox.x - bs, windowBox.y - bs, windowBox.w + 2 * bs, bs};
        r.color = color;
        g_pHyprRenderer->addPassElement(makeUnique<CRectPassElement>(r));
    }
    if (!edgeB) {
        CRectPassElement::SRectData r;
        r.box   = {windowBox.x - bs, windowBox.y + windowBox.h, windowBox.w + 2 * bs, bs};
        r.color = color;
        g_pHyprRenderer->addPassElement(makeUnique<CRectPassElement>(r));
    }
}

eDecorationType CHyprBorderDecoration::getDecorationType() {
    return DECORATION_BORDER;
}

void CHyprBorderDecoration::updateWindow(PHLWINDOW) {
    auto borderSize = m_window->getRealBorderSize();

    if (borderSize == m_lastBorderSize)
        return;

    if (borderSize <= 0 && m_lastBorderSize <= 0)
        return;

    m_lastBorderSize = borderSize;

    g_pDecorationPositioner->repositionDeco(this);
}

void CHyprBorderDecoration::damageEntire() {
    if (!validMapped(m_window) || Fullscreen::controller()->getFullscreenModes(m_window.lock()).internal != Fullscreen::FSMODE_NONE)
        return;

    const auto GLOBAL_BOX = assignedBoxGlobal();
    if (GLOBAL_BOX.w <= 0 || GLOBAL_BOX.h <= 0)
        return;

    const auto ROUNDING   = m_window->rounding();
    const auto BORDERSIZE = m_window->getRealBorderSize() + 1;

    CRegion    borderRegion(GLOBAL_BOX);
    borderRegion.subtract(GLOBAL_BOX.copy().expand(-(BORDERSIZE + ROUNDING)));
    borderRegion.expand(2); // pad

    const CBox borderExtents = borderRegion.getExtents();

    for (auto const& m : State::monitorState()->monitors()) {
        const CBox monitorBox = {m->m_position, m->m_size};
        if (borderExtents.intersection(monitorBox).empty())
            continue;

        if (!g_pHyprRenderer->shouldRenderWindow(m_window.lock(), m)) {
            const CRegion monitorRegion(monitorBox);
            borderRegion.subtract(monitorRegion);
        }
    }

    g_pHyprRenderer->damageRegion(borderRegion);
}

eDecorationLayer CHyprBorderDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CHyprBorderDecoration::getDecorationFlags() {
    static auto PPARTOFWINDOW = CConfigValue<Config::INTEGER>("decoration:border_part_of_window");

    return *PPARTOFWINDOW && !doesntWantBorders() ? DECORATION_PART_OF_MAIN_WINDOW : 0;
}

std::string CHyprBorderDecoration::getDisplayName() {
    return "Border";
}

bool CHyprBorderDecoration::doesntWantBorders() {
    return m_window->m_X11DoesntWantBorders || m_window->getRealBorderSize() == 0 || !m_window->m_ruleApplicator->decorate().valueOrDefault();
}
