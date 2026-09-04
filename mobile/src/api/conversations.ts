// Conversations, Messages, and PinkyPromise — Module C (Matching & Conversations)
// backend surface. Mirrors backend/api/openapi.yaml. Owned by Frontend Module 3;
// keep in sync with that file, do not diverge without flagging the contract change.

import { apiRequest } from './client';

export type ConversationStatus = 'active' | 'pinky_promised' | 'expired';

export type MessageType = 'text' | 'voice';

export type PinkyPromiseStatus = 'pending_b_confirm' | 'confirmed' | 'cancelled' | 'completed';

export interface Conversation {
  id: string;
  proposal_id: string;
  /** "A" -- the proposal's creator. */
  proposer_user_id: string;
  /** "B" -- the user who swiped `interested`. */
  interested_user_id: string;
  last_activity_at: string;
  /** Sender of the most recent message; `null` until the first message. */
  last_sender_id: string | null;
  status: ConversationStatus;
  created_at: string;
  /** Server-computed; ISO-8601 or `null` once `status` is `pinky_promised`. */
  expires_at: string | null;
}

export interface Message {
  id: string;
  conversation_id: string;
  sender_user_id: string;
  type: MessageType;
  content: string;
  created_at: string;
}

export interface PinkyPromise {
  id: string;
  proposal_id: string;
  conversation_id: string;
  /** The proposer / initiator ("A"). */
  user_a_id: string;
  /** The interested user, who confirms ("B"). */
  user_b_id: string;
  status: PinkyPromiseStatus;
  confirmed_at: string | null;
  created_at: string;
}

export function listConversations(): Promise<{ conversations: Conversation[] }> {
  return apiRequest<{ conversations: Conversation[] }>({ method: 'GET', path: '/conversations' });
}

export function getConversation(id: string): Promise<Conversation> {
  return apiRequest<Conversation>({ method: 'GET', path: `/conversations/${id}` });
}

export function listMessages(id: string): Promise<{ messages: Message[] }> {
  return apiRequest<{ messages: Message[] }>({ method: 'GET', path: `/conversations/${id}/messages` });
}

export function postMessage(id: string, type: MessageType, content: string): Promise<Message> {
  return apiRequest<Message>({
    method: 'POST',
    path: `/conversations/${id}/messages`,
    body: { type, content },
  });
}

export function initiatePinkyPromise(conversationId: string): Promise<PinkyPromise> {
  return apiRequest<PinkyPromise>({ method: 'POST', path: `/conversations/${conversationId}/pinky-promise` });
}

export function confirmPinkyPromise(pinkyPromiseId: string): Promise<PinkyPromise> {
  return apiRequest<PinkyPromise>({ method: 'POST', path: `/pinky-promises/${pinkyPromiseId}/confirm` });
}
