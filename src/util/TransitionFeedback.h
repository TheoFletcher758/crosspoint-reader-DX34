#pragma once

class GfxRenderer;

// Lightweight visual feedback for activity transitions.
// show() draws a centered label instantly (HALF_REFRESH).
// showWithProgress() draws a popup that can display an incrementing percentage.
// dismiss() does a HALF_REFRESH to cleanly clear any ghosting before
// the next screen is drawn.
namespace TransitionFeedback {

void show(GfxRenderer& renderer, const char* message);
void showWithProgress(GfxRenderer& renderer, const char* message);
void updateProgress(GfxRenderer& renderer, int percent);
void dismiss(GfxRenderer& renderer);
bool isActive();

}  // namespace TransitionFeedback
