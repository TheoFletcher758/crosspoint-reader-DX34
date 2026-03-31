#pragma once

class GfxRenderer;

// Lightweight visual feedback for activity transitions.
// show() draws a centered label instantly (FAST_REFRESH).
// showProgressBar() draws a thick bar at the absolute bottom of the screen.
// sweepProgressBar() animates the bar from 0% to 100% in discrete steps.
// dismiss() does a HALF_REFRESH to cleanly clear any ghosting before
// the next screen is drawn.
namespace TransitionFeedback {

void show(GfxRenderer& renderer, const char* message);
// Draw a thick progress bar at the absolute bottom edge of the screen.
// percent: 0-100.  Uses FAST_REFRESH for quick e-ink updates.
void showProgressBar(GfxRenderer& renderer, int percent);
// Animate the bar from 0% to 100% in `steps` discrete increments.
void sweepProgressBar(GfxRenderer& renderer, int steps = 5);
void dismiss(GfxRenderer& renderer);
bool isActive();

}  // namespace TransitionFeedback
