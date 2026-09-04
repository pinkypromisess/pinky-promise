// Bare-view lookup of another user's profile (GET /profile/{user_id}), added
// for Frontend Module 3 to identify the other participant in a Conversation.
// Deliberately a separate, smaller shape than the full `Profile` in
// api/types.ts (no occupation/relationship_status/verified/created_at).

import { apiRequest } from './client';
import type { Photo, Sex } from './types';

export interface ProfileBareView {
  user_id: string;
  name: string;
  sex: Sex;
  age: number;
  need_to_know_text: string;
  photos: Photo[];
}

export function getUserProfile(userId: string): Promise<ProfileBareView> {
  return apiRequest<ProfileBareView>({ method: 'GET', path: `/profile/${userId}` });
}
