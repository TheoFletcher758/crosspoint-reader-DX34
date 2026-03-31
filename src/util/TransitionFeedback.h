#pragma once

class GfxRenderer;

// Lightweight visual feedback for activity transitions.
// Draws a centered label using FAST_REFRESH (~80ms) so the user
// gets immediate confirmation that their button press was received.
namespace TransitionFeedback {

void show(GfxRenderer& renderer, const char* message);

}  // namespace TransitionFeedback
