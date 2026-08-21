import * as SecureStore from 'expo-secure-store';

// Bearer token storage. Login/auth is owned by a different module; this is
// just the read/write hook the API client needs. No login UI lives here.
const AUTH_TOKEN_KEY = 'pinky_promise_auth_token';

export async function getAuthToken(): Promise<string | null> {
  return SecureStore.getItemAsync(AUTH_TOKEN_KEY);
}

export async function setAuthToken(token: string): Promise<void> {
  await SecureStore.setItemAsync(AUTH_TOKEN_KEY, token);
}

export async function clearAuthToken(): Promise<void> {
  await SecureStore.deleteItemAsync(AUTH_TOKEN_KEY);
}
