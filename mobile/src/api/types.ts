// Mirrors backend/api/openapi.yaml (Module A — Profile & Verification).
// Keep in sync with that file; do not diverge without flagging the contract change.

export type Sex = 'male' | 'female' | 'non_binary' | 'other';

export type RelationshipStatus =
  | 'single'
  | 'married'
  | 'married_open'
  | 'married_separated'
  | 'divorced'
  | 'widowed';

export interface Photo {
  id: string;
  url: string;
  position: number;
}

export interface Profile {
  user_id: string;
  name: string;
  sex: Sex;
  age: number;
  need_to_know_text: string;
  photos: Photo[];
  occupation: string | null;
  relationship_status: RelationshipStatus | null;
  verified: boolean;
  created_at: string;
}

export interface ProfileUpsertRequest {
  name: string;
  sex: Sex;
  age: number;
  need_to_know_text: string;
  photo_urls: string[];
  occupation?: string | null;
  relationship_status?: RelationshipStatus | null;
}

export interface PhotoPatchRequest {
  add?: string[];
  remove?: string[];
}

export type VerificationDecision = 'pending' | 'pass' | 'fail';

export interface Verification {
  id: string;
  user_id: string;
  decision: VerificationDecision;
  liveness_session_id?: string | null;
  liveness_score?: number | null;
  face_match_score?: number | null;
  submitted_at: string;
  decided_at?: string | null;
}

export interface ApiErrorBody {
  error: string;
  message: string;
  details?: Array<{ error: string; message: string }>;
}

// --- Not yet part of openapi.yaml ---
// Photo upload is being added to Module A. This is the agreed response shape
// for the upload-url endpoint; there is no request/path finalized yet.
export interface PhotoUploadUrlResponse {
  upload_url: string;
  object_url: string;
  expires_at: string;
}
