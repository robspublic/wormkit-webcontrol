#ifndef WKWEBCONTROL_CTASKTEAM_H
#define WKWEBCONTROL_CTASKTEAM_H

#include "CTask.h"

// A team task. Children are its worms (CTaskWorm). We read team number, owner
// machine, and current worm to build the monitor snapshot and to identify the
// turn-holding team.
//
// Offsets are for WA 3.8.1 (wkRealTime reference).
class CTaskTeam : public CTask {
public:
    char _pad30[0x38 - 0x30];
    int  team_number_dword38;         // 0x38
    int  active_dword3C;              // 0x3C
    int  owner_byte40;                // 0x40  (machine id that owns this team)
    char _pad44[0x48 - 0x44];
    int  current_worm_number_dword48; // 0x48
    // Remaining fields not needed for the monitor.

    static void install();

    // True if this team's owner is the machine currently holding the turn.
    // (owner_byte40 == *(GameGlobal+0x726C))
    bool isTurnHolder();
    // True if this team is owned by the local machine (single-machine local
    // play: effectively always true).
    bool isOwnedByMe();

    // NOTE: the in-game team NAME is not reliably readable from CTaskTeam in
    // the reference module (no confirmed offset). The monitor labels teams by
    // team_number until a name source is reverse-engineered.
};

#endif // WKWEBCONTROL_CTASKTEAM_H
