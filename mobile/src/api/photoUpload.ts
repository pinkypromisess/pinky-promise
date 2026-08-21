import type { PhotoUploadUrlResponse } from './types';

// ---------------------------------------------------------------------------
// BLOCKED on Module A: the upload-url endpoint (agreed shape below) is being
// built now and doesn't exist in backend/api/openapi.yaml yet. `mockRequest
// PhotoUploadUrl` stands in for it so the picker UI, 6-photo gate, and
// PUT /profile flow can be built and tested end-to-end.
//
// `putFileToSignedUrl` — the actual `fetch(upload_url, { method: 'PUT', body:
// file })` — is intentionally NOT implemented: the mock's `upload_url` isn't
// a real signed URL, so there is nothing to PUT to yet. Once Module A ships
// the real endpoint, swap `mockRequestPhotoUploadUrl` for a real
// `apiRequest`/`POST` call in `requestPhotoUploadUrl` below and fill in
// `putFileToSignedUrl`; everything upstream (PhotoPicker, ProfileForm) reads
// only `object_url` and doesn't need to change.
// ---------------------------------------------------------------------------

async function mockRequestPhotoUploadUrl(localUri: string): Promise<PhotoUploadUrlResponse> {
  const fileName = localUri.split('/').pop() ?? `photo-${Date.now()}.jpg`;
  const expires = new Date(Date.now() + 15 * 60 * 1000).toISOString();
  return {
    upload_url: `https://mock-upload.pinkypromise.app/put/${encodeURIComponent(fileName)}`,
    object_url: `https://storage.googleapis.com/pinky-promise-photos/mock/${encodeURIComponent(fileName)}`,
    expires_at: expires,
  };
}

export async function requestPhotoUploadUrl(localUri: string): Promise<PhotoUploadUrlResponse> {
  return mockRequestPhotoUploadUrl(localUri);
}

export function putFileToSignedUrl(_localUri: string, _uploadUrl: string): Promise<void> {
  throw new Error(
    'putFileToSignedUrl is not implemented — blocked on Module A shipping the real ' +
      'upload-url endpoint. See src/api/photoUpload.ts.',
  );
}

export interface UploadedPhoto {
  localUri: string;
  objectUrl: string;
}

// Resolves the GCS object URL for a locally-picked photo. Skips the real PUT
// (see file header) and trusts the mocked object_url so the rest of the
// profile-creation flow can be exercised now.
export async function uploadPhoto(localUri: string): Promise<UploadedPhoto> {
  const { object_url: objectUrl } = await requestPhotoUploadUrl(localUri);
  return { localUri, objectUrl };
}
