import { pinkyPromiseAffordance } from '../src/utils/pinkyPromiseAffordance';
import type { PinkyPromise } from '../src/api/conversations';

const pending: PinkyPromise = {
  id: 'pp-1',
  proposal_id: 'prop-1',
  conversation_id: 'conv-1',
  user_a_id: 'user-a',
  user_b_id: 'user-b',
  status: 'pending_b_confirm',
  confirmed_at: null,
  created_at: '2026-08-29T00:00:00Z',
};

const confirmed: PinkyPromise = { ...pending, status: 'confirmed', confirmed_at: '2026-08-29T01:00:00Z' };

describe('pinkyPromiseAffordance', () => {
  it('shows "initiate" to A when there is no known PinkyPromise on an active conversation', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: true, pinkyPromise: null })).toBe('initiate');
  });

  it('shows "none" to B when there is no known PinkyPromise (the documented reload gap)', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: false, pinkyPromise: null })).toBe('none');
  });

  it('shows "waiting_on_b" to A once they have initiated and it is pending', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: true, pinkyPromise: pending })).toBe(
      'waiting_on_b',
    );
  });

  it('shows "confirm" to B when a PinkyPromise is pending_b_confirm -- the reverse of A\'s initiate', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: false, pinkyPromise: pending })).toBe(
      'confirm',
    );
  });

  it('shows "promised" once the PinkyPromise is confirmed, regardless of role', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: true, pinkyPromise: confirmed })).toBe(
      'promised',
    );
    expect(pinkyPromiseAffordance({ conversationStatus: 'active', isA: false, pinkyPromise: confirmed })).toBe(
      'promised',
    );
  });

  it('shows "promised" once the conversation itself is pinky_promised, regardless of local PinkyPromise state', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'pinky_promised', isA: true, pinkyPromise: null })).toBe(
      'promised',
    );
  });

  it('shows "none" on an expired conversation even for A', () => {
    expect(pinkyPromiseAffordance({ conversationStatus: 'expired', isA: true, pinkyPromise: null })).toBe('none');
  });
});
