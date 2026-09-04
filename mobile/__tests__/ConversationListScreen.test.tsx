import { cleanup, render, userEvent, waitFor } from '@testing-library/react-native';

import ConversationListScreen from '../src/screens/ConversationListScreen';

afterEach(cleanup);

// `useFocusEffect` needs a real NavigationContext (see useNavigation's
// "Couldn't find a navigation object" throw), which the hand-built
// `navigation` prop used elsewhere in this suite doesn't provide. Standard
// React Navigation testing technique: treat it as "runs on mount" here,
// since there's no focus/blur cycle in a bare render anyway.
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
const mockConversations = [
  {
    id: 'conv-1',
    proposal_id: 'prop-1',
    proposer_user_id: 'user-me',
    interested_user_id: 'user-b',
    last_activity_at: '2026-08-29T00:00:00Z',
    last_sender_id: 'user-b',
    status: 'active',
    created_at: '2026-08-28T00:00:00Z',
    expires_at: new Date(Date.now() + 1000 * 60 * 60 * 11).toISOString(), // ~11h out
  },
  {
    id: 'conv-2',
    proposal_id: 'prop-2',
    proposer_user_id: 'user-c',
    interested_user_id: 'user-me',
    last_activity_at: '2026-08-27T00:00:00Z',
    last_sender_id: null,
    status: 'pinky_promised',
    created_at: '2026-08-20T00:00:00Z',
    expires_at: null,
  },
];

const mockProfiles: Record<string, unknown> = {
  'user-b': {
    user_id: 'user-b',
    name: 'Bailey',
    sex: 'female',
    age: 27,
    need_to_know_text: '',
    photos: [{ id: 'p1', url: 'https://example.com/b.jpg', position: 0 }],
  },
  'user-c': {
    user_id: 'user-c',
    name: 'Casey',
    sex: 'male',
    age: 30,
    need_to_know_text: '',
    photos: [],
  },
};

jest.mock('../src/api/profile', () => ({
  getMyProfile: jest.fn(() => Promise.resolve({ user_id: 'user-me' })),
}));

jest.mock('../src/api/conversations', () => ({
  listConversations: jest.fn(() => Promise.resolve({ conversations: mockConversations })),
}));

jest.mock('../src/api/otherProfile', () => ({
  getUserProfile: jest.fn((userId: string) => Promise.resolve(mockProfiles[userId])),
}));

function makeNavigation() {
  return { navigate: jest.fn(), setOptions: jest.fn(), replace: jest.fn() } as any;
}

describe('<ConversationListScreen />', () => {
  it('renders a row per conversation, identifying the other participant by name', async () => {
    const navigation = makeNavigation();
    const { getByTestId } = await render(
      <ConversationListScreen navigation={navigation} route={{ key: 'k', name: 'Conversations', params: undefined }} />,
    );

    await waitFor(() => expect(getByTestId('conversation-name-conv-1')).toHaveTextContent('Bailey'));
    expect(getByTestId('conversation-name-conv-2')).toHaveTextContent('Casey');
  });

  it('shows a countdown for a non-null expiry, and a no-expiry message when expires_at is null', async () => {
    const navigation = makeNavigation();
    const { getByTestId } = await render(
      <ConversationListScreen navigation={navigation} route={{ key: 'k', name: 'Conversations', params: undefined }} />,
    );

    await waitFor(() => expect(getByTestId('conversation-status-conv-1')).toHaveTextContent(/h left/));
    expect(getByTestId('conversation-status-conv-2')).toHaveTextContent('Pinky Promised — no expiry');
  });

  it('navigates to the detail screen with the tapped conversation id', async () => {
    const user = userEvent.setup();
    const navigation = makeNavigation();
    const { getByTestId } = await render(
      <ConversationListScreen navigation={navigation} route={{ key: 'k', name: 'Conversations', params: undefined }} />,
    );

    await waitFor(() => expect(getByTestId('conversation-row-conv-1')).toBeTruthy());
    await user.press(getByTestId('conversation-row-conv-1'));

    expect(navigation.navigate).toHaveBeenCalledWith('ConversationDetail', { conversationId: 'conv-1' });
  });
});
