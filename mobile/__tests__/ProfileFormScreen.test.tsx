import { cleanup, fireEvent, render, userEvent, waitFor } from '@testing-library/react-native';

import ProfileFormScreen from '../src/screens/ProfileFormScreen';

afterEach(cleanup);

jest.mock('../src/api/profile', () => ({
  getMyProfile: jest.fn(),
  putProfile: jest.fn().mockResolvedValue({}),
}));

jest.mock('../src/api/photoUpload', () => ({
  uploadPhoto: jest.fn((localUri: string) =>
    Promise.resolve({ localUri, objectUrl: `https://storage.googleapis.com/mock/${localUri}` }),
  ),
}));

jest.mock('expo-image-picker', () => ({
  requestMediaLibraryPermissionsAsync: jest.fn().mockResolvedValue({ granted: true }),
  launchImageLibraryAsync: jest.fn(),
}));

const ImagePicker = jest.requireMock('expo-image-picker');

function makeNavigation() {
  return {
    setOptions: jest.fn(),
    replace: jest.fn(),
    navigate: jest.fn(),
  } as any;
}

async function fillRequiredFields(getByText: any, getByPlaceholderText: any) {
  await fireEvent.changeText(getByPlaceholderText('Your name'), 'Jamie');
  await fireEvent.press(getByText('Male'));
  await fireEvent.changeText(getByPlaceholderText('Your age'), '28');
  await fireEvent.changeText(
    getByPlaceholderText('What should someone know before meeting you?'),
    'I love hiking.',
  );
}

async function addPhotos(count: number, user: ReturnType<typeof userEvent.setup>, getByText: any, getByTestId: any) {
  let uriCounter = 0;
  ImagePicker.launchImageLibraryAsync.mockImplementation(() => {
    uriCounter += 1;
    return Promise.resolve({
      canceled: false,
      assets: [{ uri: `file://photo-${uriCounter}.jpg` }],
    });
  });

  for (let i = 0; i < count; i += 1) {
    // eslint-disable-next-line no-await-in-loop
    await user.press(getByText('Choose from Library'));
    // eslint-disable-next-line no-await-in-loop
    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent(`${i + 1} / 6 minimum`));
  }
}

describe('<ProfileFormScreen /> 6-photo submit gate', () => {
  it('keeps Create Profile disabled with fewer than 6 photos', async () => {
    const user = userEvent.setup();
    const navigation = makeNavigation();
    const { getByText, getByPlaceholderText, getByTestId, getByRole } = await render(
      <ProfileFormScreen navigation={navigation} route={{ key: 'k', name: 'ProfileForm', params: { mode: 'create' } }} />,
    );

    await fillRequiredFields(getByText, getByPlaceholderText);
    await addPhotos(5, user, getByText, getByTestId);

    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent('5 / 6 minimum'));

    const submitButton = getByRole('button', { name: 'Create Profile' });
    expect(submitButton.props.accessibilityState?.disabled).toBe(true);
  });

  it('enables Create Profile once 6 photos have finished uploading', async () => {
    const user = userEvent.setup();
    const navigation = makeNavigation();
    const { getByText, getByPlaceholderText, getByTestId, getByRole } = await render(
      <ProfileFormScreen navigation={navigation} route={{ key: 'k', name: 'ProfileForm', params: { mode: 'create' } }} />,
    );

    await fillRequiredFields(getByText, getByPlaceholderText);
    await addPhotos(6, user, getByText, getByTestId);

    await waitFor(() => expect(getByTestId('photo-count')).toHaveTextContent('6 / 6 minimum'));

    await waitFor(() => {
      const submitButton = getByRole('button', { name: 'Create Profile' });
      expect(submitButton.props.accessibilityState?.disabled).toBeFalsy();
    });

    await user.press(getByText('Create Profile'));
    await waitFor(() => expect(navigation.replace).toHaveBeenCalledWith('ProfileHome'));
  });
});
