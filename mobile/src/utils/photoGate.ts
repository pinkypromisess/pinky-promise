export const MIN_PROFILE_PHOTOS = 6;

export function isPhotoCountValid(photoCount: number): boolean {
  return photoCount >= MIN_PROFILE_PHOTOS;
}
