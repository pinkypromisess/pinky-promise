import { cleanup, render, userEvent, waitFor } from '@testing-library/react-native';

import { ApiError } from '../src/api/client';
import ConversationDetailScreen from '../src/screens/ConversationDetailScreen';

afterEach(cleanup);
afterEach(() => jest.clearAllMocks());

// See ConversationListScreen.test.tsx for why useFocusEffect is mocked to
// "run on mount" here.
jest.mock('@react-navigation/native', () => {
  const actual = jest.requireActual('@react-navigation/native');
  return {
    ...actual,
    useFocusEffect: (effect: () => void | (() => void)) => {
      const { useEffect } = require('react');
      useEffect(effect, []);
    },
  };
});

// jest.mock factories may only reference out-of-scope variables prefixed
// with `mock` (case-insensitive) -- hence the naming below.
const mockConversationActiveAViewer = {
  id: 'conv-1',
  proposal_id: 'prop-1',
  proposer_user_id: 'user-me', // viewer is A in this fixture
  interested_user_id: 'user-b',
  last_activity_at: '2026-08-29T00:00:00Z',
  last_sender_id: 'user-b',
  status: 'active',
  created_at: '2026-08-28T00:00:00Z',
  expires_at: new Date(Date.now() + 1000 * 60 * 60 * 11).toISOString(), // ~11h out
};

const mockConversationActiveBViewer = {
  ...mockConversationActiveAViewer,
  proposer_user_id: 'user-other', // viewer ('user-me') is B in this fixture
  interested_user_id: 'user-me',
};

const mockMessages = [
  {
    id: 'm1',
    conversation_id: 'conv-1',
    sender_user_id: 'user-b',
    type: 'text',
    content: 'Hey there!',
    created_at: '2026-08-28T01:00:00Z',
  },
  {
    id: 'm2',
    conversation_id: 'conv-1',
    sender_user_id: 'user-me',
    type: 'text',
    content: 'Hi, nice to meet you.',
    created_at: '2026-08-28T02:00:00Z',
  },
];

const mockOtherProfile = {
  user_id: 'user-b',
  name: 'Bailey',
  sex: 'female',
  age: 27,
  need_to_know_text: '',
  photos: [],
};

jest.mock('../src/api/profile', () => ({
  getMyProfile: jest.fn(() => Promise.resolve({ user_id: 'user-me' })),
}));

jest.mock('../src/api/otherProfile', () => ({
  getUserProfile: jest.fn(() => Promise.resolve(mockOtherProfile)),
}));

const mockGetConversation = jest.fn();
const mockListMessages = jest.fn();
const mockPostMessage = jest.fn();
const mockInitiatePinkyPromise = jest.fn();
const mockConfirmPinkyPromise = jest.fn();

jest.mock('../src/api/conversations', () => ({
  getConversation: (...args: unknown[]) => mockGetConversation(...args),
  listMessages: (...args: unknown[]) => mockListMessages(...args),
  postMessage: (...args: unknown[]) => mockPostMessage(...args),
  initiatePinkyPromise: (...args: unknown[]) => mockInitiatePinkyPromise(...args),
  confirmPinkyPromise: (...args: unknown[]) => mockConfirmPinkyPromise(...args),
}));

function makeNavigation() {
  return { navigate: jest.fn(), setOptions: jest.fn(), replace: jest.fn() } as any;
}

function makeRoute() {
  return { key: 'k', name: 'ConversationDetail', params: { conversationId: 'conv-1' } } as any;
}

beforeEach(() => {
  mockGetConversation.mockResolvedValue(mockConversationActiveAViewer);
  mockListMessages.mockResolvedValue({ messages: mockMessages });
});

describe('<ConversationDetailScreen /> message thread', () => {
  it('renders message history oldest to newest', async () => {
    const { getByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('message-m1')).toBeTruthy());
    expect(getByTestId('message-m1')).toHaveTextContent('Hey there!');
    expect(getByTestId('message-m2')).toHaveTextContent('Hi, nice to meet you.');
  });

  it('sends a message via the API and appends it to the thread', async () => {
    mockPostMessage.mockResolvedValue({
      id: 'm3',
      conversation_id: 'conv-1',
      sender_user_id: 'user-me',
      type: 'text',
      content: 'Looking forward to it!',
      created_at: '2026-08-29T03:00:00Z',
    });

    const user = userEvent.setup();
    const { getByTestId, getByText } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('composer-input')).toBeTruthy());
    await user.type(getByTestId('composer-input'), 'Looking forward to it!');
    await user.press(getByText('Send'));

    await waitFor(() => expect(mockPostMessage).toHaveBeenCalledWith('conv-1', 'text', 'Looking forward to it!'));
    await waitFor(() => expect(getByTestId('message-m3')).toHaveTextContent('Looking forward to it!'));
  });

  it('shows a clear message instead of crashing on 409 CONVERSATION_EXPIRED', async () => {
    mockPostMessage.mockRejectedValue(
      new ApiError(409, { error: 'CONVERSATION_EXPIRED', message: 'This conversation has expired.' }),
    );

    const user = userEvent.setup();
    const { getByTestId, getByText } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('composer-input')).toBeTruthy());
    await user.type(getByTestId('composer-input'), 'Still there?');
    await user.press(getByText('Send'));

    await waitFor(() => expect(getByTestId('send-error')).toHaveTextContent('This conversation has expired.'));
  });
});

describe('<ConversationDetailScreen /> expiry countdown', () => {
  it('renders a countdown when expires_at is non-null', async () => {
    const { getByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('expiry-countdown')).toHaveTextContent(/h left/));
  });

  it('renders a no-expiry message when expires_at is null', async () => {
    mockGetConversation.mockResolvedValue({ ...mockConversationActiveAViewer, status: 'pinky_promised', expires_at: null });

    const { getByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('expiry-countdown')).toHaveTextContent('Pinky Promised — no expiry'));
  });
});

describe('<ConversationDetailScreen /> Pinky Promise flow', () => {
  it('shows the initiate button to A, and calling it stores the pending PinkyPromise', async () => {
    mockInitiatePinkyPromise.mockResolvedValue({
      id: 'pp-1',
      proposal_id: 'prop-1',
      conversation_id: 'conv-1',
      user_a_id: 'user-me',
      user_b_id: 'user-b',
      status: 'pending_b_confirm',
      confirmed_at: null,
      created_at: '2026-08-29T04:00:00Z',
    });

    const user = userEvent.setup();
    const { getByTestId, queryByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('pinky-promise-initiate')).toBeTruthy());
    expect(queryByTestId('pinky-promise-confirm')).toBeNull();

    await user.press(getByTestId('pinky-promise-initiate'));

    await waitFor(() => expect(mockInitiatePinkyPromise).toHaveBeenCalledWith('conv-1'));
    await waitFor(() => expect(getByTestId('pinky-promise-waiting')).toBeTruthy());
    expect(queryByTestId('pinky-promise-initiate')).toBeNull();
  });

  it('does not show the initiate button to B', async () => {
    mockGetConversation.mockResolvedValue(mockConversationActiveBViewer);

    const { queryByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(queryByTestId('composer-input')).toBeTruthy());
    expect(queryByTestId('pinky-promise-initiate')).toBeNull();
  });

  it('handles 409 PINKY_PROMISE_EXISTS on initiate cleanly instead of crashing', async () => {
    // Covers the documented gap: after a re-focus/app-restart there is no
    // way to know a PinkyPromise is already pending, so A's initiate button
    // can show again; the backend correctly rejects the retry with 409
    // PINKY_PROMISE_EXISTS, which must be handled cleanly.
    mockInitiatePinkyPromise.mockRejectedValue(
      new ApiError(409, { error: 'PINKY_PROMISE_EXISTS', message: 'A PinkyPromise already exists.' }),
    );

    const user = userEvent.setup();
    const { getByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('pinky-promise-initiate')).toBeTruthy());
    await user.press(getByTestId('pinky-promise-initiate'));

    await waitFor(() =>
      expect(getByTestId('pinky-promise-error')).toHaveTextContent(
        "There's already a pending Pinky Promise on this conversation.",
      ),
    );
    // Did not crash, and the initiate button is still there to retry/ignore.
    expect(getByTestId('pinky-promise-initiate')).toBeTruthy();
  });

  it('shows the "Pinky Promised!" badge and no buttons once the conversation is pinky_promised', async () => {
    mockGetConversation.mockResolvedValue({ ...mockConversationActiveAViewer, status: 'pinky_promised', expires_at: null });

    const { getByTestId, queryByTestId } = await render(
      <ConversationDetailScreen navigation={makeNavigation()} route={makeRoute()} />,
    );

    await waitFor(() => expect(getByTestId('pinky-promise-badge')).toHaveTextContent('Pinky Promised!'));
    expect(queryByTestId('pinky-promise-initiate')).toBeNull();
    expect(queryByTestId('pinky-promise-confirm')).toBeNull();
  });

  // The confirm button is only ever shown to B when this component's local
  // `pinkyPromise` state already holds one in `pending_b_confirm` (see the
  // KNOWN GAP note atop ConversationDetailScreen.tsx: there is no endpoint
  // for B to discover a pending PinkyPromise they didn't create themselves,
  // and B can never call initiate, so that state is not reachable through
  // UI interaction alone in a fresh render). The branch-selection logic for
  // that case (isA=false + pending -> 'confirm', including what happens on
  // a 409 from confirm) is exercised directly against the pure decision
  // function and confirm handler wiring in pinkyPromiseAffordance.test.ts
  // instead of being faked here through a non-reachable component state.
});
