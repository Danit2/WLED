#pragma once

#include "wled.h"
#include "fseq_player.h"

//
// FSEQ Player effect – renders FSEQ frame data into the active SEGMENT.
//
// File selection follows the same pattern as the built-in Image effect:
// set the **segment name** to the .fseq filename on the SD card
// (e.g. "show.fseq").  The effect prepends "/" and opens the file.
//
// Check1 enables loop mode (repeat the sequence forever).
//

// Stored per-effect so we can detect when the segment name changes.
static char _fseq_lastName[WLED_MAX_SEGNAME_LEN + 2] = "";

static void mode_fseq_player(void) {
  // Build the target filename from the segment name
  const char *segName = SEGMENT.name;

  // No name set – show static colour
  if (!segName || segName[0] == '\0') {
    if (FSEQPlayer::isPlaying()) FSEQPlayer::clearLastPlayback();
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }

  // Detect segment-name change → (re)load the file
  if (strncmp(_fseq_lastName, segName, WLED_MAX_SEGNAME_LEN) != 0) {
    strncpy(_fseq_lastName, segName, WLED_MAX_SEGNAME_LEN);
    _fseq_lastName[WLED_MAX_SEGNAME_LEN] = '\0';

    // Build "/<name>" path
    char path[WLED_MAX_SEGNAME_LEN + 2];
    path[0] = '/';
    strncpy(path + 1, segName, WLED_MAX_SEGNAME_LEN);
    path[WLED_MAX_SEGNAME_LEN + 1] = '\0';

    bool loop = SEGMENT.check1;
    FSEQPlayer::loadRecording(path, 0.0f, loop);
  }

  if (!FSEQPlayer::isPlaying()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }

  // Keep loop state in sync with the UI checkbox
  FSEQPlayer::setLooping(SEGMENT.check1);

  // Render the current frame into the segment
  FSEQPlayer::renderFrameToSegment();
}

static const char _data_FX_MODE_FSEQ_PLAYER[] PROGMEM =
    "FSEQ Player@,,,,,Loop;;!;1";

