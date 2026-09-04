#ifndef WKWEBCONTROL_CTASKTEAM_H
#define WKWEBCONTROL_CTASKTEAM_H

#include "CTask.h"

// A team task. We use it to identify team ownership (which machine controls it)
// and whether it currently holds the turn.
//
// TODO(offsets): confirm layout against WA 3.8.1.
class CTaskTeam : public CTask {
public:
    char _pad30[0x38 - 0x30];
    int  team_number_dword38;        // 0x38
    int  active_dword3C;             // 0x3C
    int  owner_byte40;               // 0x40 (local machine id owns == controllable here)
    char _pad44[0x48 - 0x44];
    int  current_worm_number_dword48;// 0x48
    // Remaining fields (weapon flags, etc.) omitted from the initial scaffold.

    static void install();

    bool isOwnedByMe();
    bool isTurnHolder();

    // The team name as shown in-game; used to match against the web/admin
    // email mapping. TODO(offsets): locate the team-name field/getter.
    const char* getName() const;
};

#endif // WKWEBCONTROL_CTASKTEAM_H
