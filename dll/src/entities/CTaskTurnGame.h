#ifndef WKWEBCONTROL_CTASKTURNGAME_H
#define WKWEBCONTROL_CTASKTURNGAME_H

#include "CTask.h"

// The turn/game-flow task, root for enumerating teams/worms. Reached via
// W2App::getAddrTurnGame() = *(DdGame + 0x8).
//
// Offsets are for WA 3.8.1 (wkRealTime reference).
class CTaskTurnGame : public CTask {
public:
    char _pad30[0x7C - 0x30];
    int  number_of_teams_dword7C;         // 0x7C
    char _pad80[0x140 - 0x80];
    int  its_before_round_start_dword140; // 0x140 (worms still being placed)
    char _pad144[0x150 - 0x144];
    int  turn_paused_dword150;            // 0x150 (named; not confirmed-read)
    char _pad154[0x178 - 0x154];
    int  retreat_timer_dword178;          // 0x178 (named; not confirmed-read)
    char _pad17C[0x184 - 0x17C];
    int  round_timer_dword184;            // 0x184 (named; not confirmed-read)
    int  turn_timer1_unknown188;          // 0x188 (turn timer, ms)
    int  turn_timer2_unknown18C;          // 0x18C (turn timer, ms)
    char _pad190[0x2E0 - 0x190];

    static void install();

    // The team number currently taking its turn, or -1 if none. Tracked from
    // StartTurn/TurnStarted messages (the machine-id approach can't distinguish
    // teams in single-machine local play, where all teams share one machine).
    static int currentTurnTeam();
    static void clearTurn();  // called on game teardown

private:
    static int __fastcall hookTurnHandleMessage(CTaskTurnGame* This, int EDX,
                                                CTask* sender,
                                                Constants::TaskMessage mtype,
                                                size_t size, void* data);
};

#endif // WKWEBCONTROL_CTASKTURNGAME_H
