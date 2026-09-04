#ifndef WKWEBCONTROL_CTASKTURNGAME_H
#define WKWEBCONTROL_CTASKTURNGAME_H

#include "CTask.h"

// The turn/game-flow task. This answers "whose turn is it right now", which
// gates which web user is allowed to send input.
//
// TODO(offsets): confirm layout against WA 3.8.1.
class CTaskTurnGame : public CTask {
public:
    char _pad30[0x7C - 0x30];
    int  number_of_teams_dword7C;        // 0x7C
    char _pad80[0x12C - 0x80];
    int  current_team_1_unknown12C;      // 0x12C
    char _pad130[0x134 - 0x130];
    int  current_team_2_unknown134;      // 0x134
    char _pad138[0x140 - 0x138];
    int  its_before_round_start_dword140;// 0x140
    char _pad144[0x150 - 0x144];
    int  turn_paused_dword150;           // 0x150
    char _pad154[0x178 - 0x154];
    int  retreat_timer_dword178;         // 0x178
    char _pad17C[0x184 - 0x17C];
    int  round_timer_dword184;           // 0x184
    int  turn_timer1_unknown188;         // 0x188
    int  turn_timer2_unknown18C;         // 0x18C
    char _pad190[0x2E0 - 0x190];

    static void install();

    // Index of the team currently holding the turn, or -1 if none / round not
    // started. Derived from current_team_* fields.
    int currentTurnTeamNumber();
};

#endif // WKWEBCONTROL_CTASKTURNGAME_H
