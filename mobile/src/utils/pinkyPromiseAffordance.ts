// Pure decision logic for which Pinky Promise affordance to render on the
// Conversation detail screen (CUJ #4). Kept separate from the screen
// component so it's directly unit-testable without needing two live-mounted
// screens to stand in for both participants at once — see the note on the
// "no way to reload a pending PinkyPromise" gap in ConversationDetailScreen.tsx.

import type { ConversationStatus, PinkyPromise } from '../api/conversations';

export type PinkyPromiseAffordance =
  /** Viewer is A ("proposer"); no known PinkyPromise yet -- show the initiate button. */
  | 'initiate'
  /** Viewer is A; a PinkyPromise they initiated is awaiting B's confirmation. */
  | 'waiting_on_b'
  /** Viewer is B ("interested user"); a PinkyPromise is awaiting their confirmation. */
  | 'confirm'
  /** The Conversation (or its PinkyPromise) is confirmed/finalized. */
  | 'promised'
  /** Nothing to show (e.g. B with no known pending PinkyPromise, or expired). */
  | 'none';

export function pinkyPromiseAffordance(params: {
  conversationStatus: ConversationStatus;
  isA: boolean;
  pinkyPromise: PinkyPromise | null;
}): PinkyPromiseAffordance {
  const { conversationStatus, isA, pinkyPromise } = params;

  if (conversationStatus === 'pinky_promised') return 'promised';
  if (pinkyPromise?.status === 'confirmed') return 'promised';

  if (conversationStatus === 'expired') return 'none';

  if (pinkyPromise?.status === 'pending_b_confirm') {
    return isA ? 'waiting_on_b' : 'confirm';
  }

  return isA ? 'initiate' : 'none';
}
