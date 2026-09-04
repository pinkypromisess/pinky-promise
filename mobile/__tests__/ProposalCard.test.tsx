import { cleanup, render } from '@testing-library/react-native';

import ProposalCard from '../src/components/ProposalCard';
import type { ProposalFeedItem } from '../src/api/proposals';

afterEach(cleanup);

function makeItem(overrides: Partial<ProposalFeedItem['creator']> = {}): ProposalFeedItem {
  return {
    proposal: {
      id: 'prop_1',
      creator_user_id: 'u_creator',
      activity_text: 'go bowling',
      event_time: '2026-09-10T18:00:00Z',
      location: { lat: 1, lng: 1, address: '123 Main St' },
      payment_type: 'host_treats',
      looking_for_text: 'someone chill',
      revealed_fields: ['occupation'],
      status: 'active',
      created_at: '2026-09-01T00:00:00Z',
    },
    creator: {
      name: 'Jordan',
      sex: 'female',
      age: 27,
      need_to_know_text: 'I love bowling.',
      photos: Array.from({ length: 6 }, (_, i) => ({
        id: `photo_${i}`,
        url: `https://storage.googleapis.com/mock/photo-${i}.jpg`,
        position: i,
      })),
      occupation: 'Chef',
      relationship_status: null,
      ...overrides,
    },
  };
}

describe('<ProposalCard />', () => {
  it('renders required profile fields (1-5), proposal fields, and interleaves photos with info blocks', async () => {
    const item = makeItem();
    const { getByText, getAllByTestId } = await render(<ProposalCard item={item} />);

    // Profile fields 1-5.
    expect(getByText(/Jordan/)).toBeTruthy();
    expect(getByText('female')).toBeTruthy();
    expect(getByText('I love bowling.')).toBeTruthy();
    expect(getAllByTestId('proposal-photo')).toHaveLength(6);

    // Proposal-specific fields.
    expect(getByText('go bowling')).toBeTruthy();
    expect(getByText('123 Main St')).toBeTruthy();
    expect(getByText("I'm treating")).toBeTruthy();
    expect(getByText('someone chill')).toBeTruthy();

    // Revealed optional field.
    expect(getByText('Chef')).toBeTruthy();
  });

  it('interleaves photos and info blocks: photo, block, photo, block, ...', async () => {
    const item = makeItem();
    const rendered = await render(<ProposalCard item={item} />);

    const card = rendered.getByTestId('proposal-card');
    // react-test-renderer's JSON tree gives each top-level child of the card
    // in document/scroll order -- assert it strictly alternates Image/View.
    const topLevelTypes = (card.children as unknown[]).map((child) =>
      typeof child === 'object' && child !== null ? (child as { type: string }).type : child,
    );

    expect(topLevelTypes.length).toBeGreaterThanOrEqual(4);
    expect(topLevelTypes[0]).toBe('Image');
    for (let i = 0; i < topLevelTypes.length; i += 1) {
      expect(topLevelTypes[i]).toBe(i % 2 === 0 ? 'Image' : 'View');
    }
  });

  it('omits occupation/relationship_status blocks when neither is revealed', async () => {
    const item = makeItem({ occupation: null, relationship_status: null });
    const { queryByText } = await render(<ProposalCard item={item} />);

    expect(queryByText('Occupation')).toBeNull();
    expect(queryByText('Relationship status')).toBeNull();
  });
});
