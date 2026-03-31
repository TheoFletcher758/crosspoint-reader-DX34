#pragma once

class GfxRenderer;

// Lightweight visual feedback for activity transitions.
// show() draws a centered label instantly (FAST_REFRESH).
// dismiss() does a HALF_REFRESH to cleanly clear any ghosting before
// the next screen is drawn.
namespace TransitionFeedback {

void show(GfxRenderer& renderer, const char* message);
void dismiss(GfxRenderer& renderer);

}  // namespace TransitionFeedback
