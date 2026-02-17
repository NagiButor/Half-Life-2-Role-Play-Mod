// Custom sound channel definitions for mod-specific sounds.
#pragma once

// Define a dedicated breathing channel using CHAN_USER_BASE to avoid
// colliding with engine channels like CHAN_BODY/CHAN_ITEM/CHAN_VOICE.
#ifndef CHAN_BREATHING
#define CHAN_BREATHING (CHAN_USER_BASE + 1)
#endif
