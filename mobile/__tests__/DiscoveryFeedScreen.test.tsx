import { cleanup, render, userEvent, waitFor } from '@testing-library/react-native';

import DiscoveryFeedScreen from '../src/screens/DiscoveryFeedScreen';
import type { ProposalFeedItem } from '../src/api/proposals';

afterEach(cleanup);
beforeEach(() => jest.clearAllMocks());

// The screen is rendered standalone (no NavigationContainer ancestor), same
// as this codebase's other screen tests -- stand `useFocusEffect` in for a
// plain mount-time effect so it doesn't require navigation context.
jest.mock('@react-navigation/native', () => ({
  useFocusEffect: (effect: () => void | (() => void)) => {
    // eslint-disable-next-line react-hooks/rules-of-hooks, @typescript-eslint/no-var-requires
    require('react').useEffect(effect, []);
  },
}));

function makeProfile(overrides: Partial<{ verified: boolean }> = {}) {
  return {
    user_id: 'u_me',
    name: 'Me',
    sex: 'other',
    age: 30,
    need_to_know_text: 'n/a',
    photos: [],
    occupation: null,
    relationship_status: null,
    verified: false,
    created_at: '2026-01-01T00:00:00Z',
    ...overrides,
  };
}

function makeItem(overrides: Partial<ProposalFeedItem['proposal']> = {}, id = 'prop_1'): ProposalFeedItem {
  return {
    proposal: {
      id,
      creator_user_id: 'u_creator',
      activity_text: 'go bowling',
      event_time: '2026-09-10T18:00:00Z',
      location: { lat: 1, lng: 1, address: '123 Main St' },
      payment_type: 'split',
      looking_for_text: 'someone chill',
      revealed_fields: [],
      status: 'active',
      created_at: '2026-09-01T00:00:00Z',
      ...overrides,
    },
    creator: {
      name: 'Jordan',
      sex: 'female',
      age: 27,
      need_to_know_text: 'I love bowling.',
      photos: Array.from({ length: 6 }, (_, i) => ({
        id: `photo_${i}`,
        url: `https://storage.googleapis.com/mock/${id}-${i}.jpg`,
        position: i,
      })),
      occupation: null,
      relationship_status: null,
    },
  };
}

jest.mock('../src/api/profile', () => ({
  getMyProfile: jest.fn(),
}));

jest.mock('../src/api/proposals', () => ({
  getFeed: jest.fn(),
  swipeOnProposal: jest.fn(),
}));

const { getMyProfile } = jest.requireMock('../src/api/profile');
const { getFeed, swipeOnProposal } = jest.requireMock('../src/api/proposals');

function makeNavigation() {
  return { setOptions: jest.fn(), replace: jest.fn(), navigate: jest.fn() } as any;
}

async function renderScreen(navigation = makeNavigation()) {
  const utils = await render(
    <DiscoveryFeedScreen navigation={navigation} route={{ key: 'k', name: 'Discovery', params: undefined }} />,
  );
  return { ...utils, navigation };
}

describe('<DiscoveryFeedScreen />', () => {
  it('renders the current feed item\'s key fields', async () => {
    getFeed.mockResolvedValue({ proposals: [makeItem()] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));

    const { getByText } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());
    expect(getByText(/Jordan/)).toBeTruthy();
    expect(getByText('123 Main St')).toBeTruthy();
    expect(getByText('someone chill')).toBeTruthy();
  });

  it('tapping X passes and advances to the next item', async () => {
    const user = userEvent.setup();
    getFeed.mockResolvedValue({ proposals: [makeItem({}, 'prop_1'), makeItem({ activity_text: 'go hiking' }, 'prop_2')] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));
    swipeOnProposal.mockResolvedValue({ id: 's1', proposal_id: 'prop_1', action: 'pass', created_at: 'now' });

    const { getByText, getByLabelText } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());

    await user.press(getByLabelText('Pass'));

    await waitFor(() => expect(swipeOnProposal).toHaveBeenCalledWith('prop_1', 'pass'));
    await waitFor(() => expect(getByText('go hiking')).toBeTruthy());
  });

  it('tapping Heart when verified calls interested swipe and advances', async () => {
    const user = userEvent.setup();
    getFeed.mockResolvedValue({ proposals: [makeItem({}, 'prop_1'), makeItem({ activity_text: 'go hiking' }, 'prop_2')] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));
    swipeOnProposal.mockResolvedValue({ id: 's1', proposal_id: 'prop_1', action: 'interested', created_at: 'now' });

    const { getByText, getByLabelText, navigation } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());

    await user.press(getByLabelText('Heart'));

    await waitFor(() => expect(swipeOnProposal).toHaveBeenCalledWith('prop_1', 'interested'));
    await waitFor(() => expect(getByText('go hiking')).toBeTruthy());
    expect(navigation.navigate).not.toHaveBeenCalledWith('Verification');
  });

  it('tapping Heart when unverified navigates to Verification and does not swipe', async () => {
    const user = userEvent.setup();
    getFeed.mockResolvedValue({ proposals: [makeItem()] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: false }));

    const { getByText, getByLabelText, navigation } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());

    await user.press(getByLabelText('Heart'));

    await waitFor(() => expect(navigation.navigate).toHaveBeenCalledWith('Verification'));
    expect(swipeOnProposal).not.toHaveBeenCalled();
  });

  it('shows an empty-state message when the feed has no items', async () => {
    getFeed.mockResolvedValue({ proposals: [] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));

    const { getByTestId } = await renderScreen();

    await waitFor(() => expect(getByTestId('empty-state')).toBeTruthy());
  });

  it('shows a clear message and does not crash on a daily-cap 403 from swipe', async () => {
    const user = userEvent.setup();
    const { ApiError } = jest.requireActual('../src/api/client');
    getFeed.mockResolvedValue({ proposals: [makeItem()] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));
    swipeOnProposal.mockRejectedValue(
      new ApiError(403, { error: 'INTERESTED_DAILY_CAP_REACHED', message: 'Daily cap reached.' }),
    );

    const alertSpy = jest.spyOn(require('react-native').Alert, 'alert').mockImplementation(() => {});

    const { getByText, getByLabelText } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());

    await user.press(getByLabelText('Heart'));

    await waitFor(() => expect(alertSpy).toHaveBeenCalled());
    // Did not crash, and stayed on the same (only) item since interest wasn't recorded.
    expect(getByText('go bowling')).toBeTruthy();

    alertSpy.mockRestore();
  });

  it('handles an ALREADY_SWIPED 409 from swipe without crashing, by advancing', async () => {
    const user = userEvent.setup();
    const { ApiError } = jest.requireActual('../src/api/client');
    getFeed.mockResolvedValue({ proposals: [makeItem({}, 'prop_1'), makeItem({ activity_text: 'go hiking' }, 'prop_2')] });
    getMyProfile.mockResolvedValue(makeProfile({ verified: true }));
    swipeOnProposal.mockRejectedValue(new ApiError(409, { error: 'ALREADY_SWIPED', message: 'Already swiped.' }));

    const { getByText, getByLabelText } = await renderScreen();

    await waitFor(() => expect(getByText('go bowling')).toBeTruthy());

    await user.press(getByLabelText('Pass'));

    await waitFor(() => expect(getByText('go hiking')).toBeTruthy());
  });
});
