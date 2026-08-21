import { apiRequest } from './client';
import type { PhotoPatchRequest, Profile, ProfileUpsertRequest } from './types';

export function getMyProfile(): Promise<Profile> {
  return apiRequest<Profile>({ method: 'GET', path: '/profile/me' });
}

export function putProfile(body: ProfileUpsertRequest): Promise<Profile> {
  return apiRequest<Profile>({ method: 'PUT', path: '/profile', body });
}

export function patchProfilePhotos(body: PhotoPatchRequest): Promise<Profile> {
  return apiRequest<Profile>({ method: 'PATCH', path: '/profile/photos', body });
}
