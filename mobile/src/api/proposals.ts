// Mirrors backend/api/openapi.yaml (Module B — Proposals & Feed, Module C —
// Matching & Swipe). Deliberately kept separate from `api/types.ts`, which is
// scoped to Module A / Profile & Verification — do not add Proposal/Swipe
// shapes there. Keep this file in sync with openapi.yaml; flag, don't
// diverge, if the contract changes.

import { apiRequest } from './client';
import type { RelationshipStatus, Sex, Photo } from './types';

export type PaymentType = 'split' | 'host_treats' | 'guest_treats' | 'tbd';

export type ProposalStatus = 'active' | 'pinky_promised' | 'cancelled' | 'expired';

export type RevealableField = 'occupation' | 'relationship_status';

export interface ProposalLocation {
  lat: number;
  lng: number;
  address: string;
}

export interface Proposal {
  id: string;
  creator_user_id: string;
  activity_text: string;
  event_time: string;
  location: ProposalLocation;
  payment_type: PaymentType;
  looking_for_text: string;
  revealed_fields: RevealableField[];
  status: ProposalStatus;
  created_at: string;
}

// The subset of the creator's Profile shown on a feed card — always the
// required-to-create fields (1-5), plus occupation/relationship_status when
// the backend has computed them as revealed for this specific Proposal.
export interface ProposalCardCreatorProfile {
  name: string;
  sex: Sex;
  age: number;
  need_to_know_text: string;
  photos: Photo[];
  occupation: string | null;
  relationship_status: RelationshipStatus | null;
}

export interface ProposalFeedItem {
  proposal: Proposal;
  creator: ProposalCardCreatorProfile;
}

export interface ProposalFeedResponse {
  proposals: ProposalFeedItem[];
}

export type SwipeAction = 'interested' | 'pass';

export interface Swipe {
  id: string;
  proposal_id: string;
  action: SwipeAction;
  created_at: string;
}

// CUJ #3: browse feed, ranked by distance (if lat/lng given) then recency,
// else recency alone. Already excludes the caller's own Proposals and (per
// backend) proposals the caller has already swiped on — no client-side
// re-filtering needed.
export function getFeed(lat?: number, lng?: number): Promise<ProposalFeedResponse> {
  const params = new URLSearchParams();
  if (lat !== undefined) params.set('lat', String(lat));
  if (lng !== undefined) params.set('lng', String(lng));
  const query = params.toString();
  return apiRequest<ProposalFeedResponse>({
    method: 'GET',
    path: `/proposals/feed${query ? `?${query}` : ''}`,
  });
}

// CUJ #3: X (pass, always allowed) or Heart (interested, requires
// Profile.verified and the rolling-24h cap) on a single Proposal.
export function swipeOnProposal(proposalId: string, action: SwipeAction): Promise<Swipe> {
  return apiRequest<Swipe>({
    method: 'POST',
    path: `/proposals/${proposalId}/swipe`,
    body: { action },
  });
}
