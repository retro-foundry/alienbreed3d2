#ifndef AB3D2_DXR_EMITTER_HISTORY_H
#define AB3D2_DXR_EMITTER_HISTORY_H

#include <cstddef>
#include <span>

namespace ab3d2::dxr {

/*
 * A reservoir's compact emitter index remains meaningful only while every slot
 * still names the same triangle with the same sample-space area. The reservoir
 * weight already encodes the proposal PDF that generated it, so a power/PDF
 * update does not change sample identity. Alias entries are likewise only a
 * way to draw the current distribution.
 */
template <typename Emitter>
bool emitter_history_layout_compatible(std::span<const Emitter> previous,
                                       std::span<const Emitter> current)
{
    if (previous.size() != current.size()) {
        return false;
    }
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (previous[index].first_vertex != current[index].first_vertex ||
            previous[index].inverse_area != current[index].inverse_area) {
            return false;
        }
    }
    return true;
}

}  // namespace ab3d2::dxr

#endif
