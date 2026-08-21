import { cleanup, render, userEvent, waitFor } from '@testing-library/react-native';

import PhotoManagementScreen from '../src/screens/PhotoManagementScreen';

afterEach(cleanup);

const mockExistingProfile = {
  user_id: 'u_1',
  name: 'Jamie',
  sex: 'other',
  age: 28,
  need_to_know_text: 'I love hiking.',
  photos: Array.from({ length: 6 }, (_, i) => ({
    id: `p_${i}`,
    url: `https://storage.googleapis.com/mock/existing-${i}.jpg`,
    position: i,
  })),
  occupation: null,
  relationship_status: null,
  verified: true,
  created_at: '2026-01-01T00:00:00Z',
};

jest.mock('../src/api/profile', () => ({
  getMyProfile: jest.fn(() => Promise.resolve(mockExistingProfile)),
  patchProfilePhotos: jest.fn(() =>
    Promise.resolve({
      ...mockExistingProfile,
      photos: [
        ...mockExistingProfile.photos,
        { id: 'p_new', url: 'https://storage.googleapis.com/mock/new.jpg', position: 6 },
      ],
      verified: false,
    }),
  ),
}));

jest.mock('../src/api/photoUpload', () => ({
  uploadPhoto: jest.fn((localUri: string) =>
    Promise.resolve({ localUri, objectUrl: `https://storage.googleapis.com/mock/${localUri}` }),
  ),
}));

jest.mock('expo-image-picker', () => ({
  requestMediaLibraryPermissionsAsync: jest.fn().mockResolvedValue({ granted: true }),
  launchImageLibraryAsync: jest
    .fn()
    .mockResolvedValue({ canceled: false, assets: [{ uri: 'file://new-photo.jpg' }] }),
  requestCameraPermissionsAsync: jest.fn().mockResolvedValue({ granted: true }),
  launchCameraAsync: jest.fn(),
}));

const { patchProfilePhotos } = jest.requireMock('../src/api/profile');

function makeNavigation() {
  return { setOptions: jest.fn(), replace: jest.fn(), navigate: jest.fn() } as any;
}

describe('<PhotoManagementScreen /> verification-invalidated state', () => {
  it('shows no "not verified" warning before saving a photo change', async () => {
    const { getByTestId, queryByTestId } = await render(
      <PhotoManagementScreen
        navigation={makeNavigation()}
        route={{ key: 'k', name: 'PhotoManagement', params: undefined }}
      />,
    );

    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent('6 / 6 minimum'));
    expect(queryByTestId('verification-warning')).toBeNull();
  });

  it('visibly shows the "not verified" warning immediately after a successful photo edit', async () => {
    const user = userEvent.setup();
    const { getByText, getByTestId, queryByTestId } = await render(
      <PhotoManagementScreen
        navigation={makeNavigation()}
        route={{ key: 'k', name: 'PhotoManagement', params: undefined }}
      />,
    );

    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent('6 / 6 minimum'));

    await user.press(getByText('Choose from Library'));
    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent('7 / 6 minimum'));

    expect(queryByTestId('verification-warning')).toBeNull();

    await user.press(getByText('Save Changes'));

    await waitFor(() => expect(patchProfilePhotos).toHaveBeenCalledWith({ add: [expect.any(String)], remove: [] }));
    await waitFor(() => expect(getByTestId('verification-warning')).toBeTruthy());
    expect(getByTestId('verification-warning')).toHaveTextContent(/verify again/i);
  });
});
