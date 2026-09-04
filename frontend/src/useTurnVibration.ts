import { useEffect, useRef } from "react";

// Buzz the device when it becomes the user's turn, so a player watching the
// board (not the phone) gets a heads-up. Uses the Vibration API, which is
// supported on Android Chrome/Firefox; iOS Safari has no support, so this
// no-ops there. A short double pulse reads as a distinct "your turn" cue.
//
// Fires only on the rising edge (not-my-turn -> my-turn), so it buzzes once per
// turn rather than on every state snapshot.
export function useTurnVibration(isMyTurn: boolean): void {
  const wasMyTurn = useRef(false);

  useEffect(() => {
    if (isMyTurn && !wasMyTurn.current) {
      // pattern: vibrate 200ms, pause 100ms, vibrate 200ms
      navigator.vibrate?.([200, 100, 200]);
    }
    wasMyTurn.current = isMyTurn;
  }, [isMyTurn]);
}
