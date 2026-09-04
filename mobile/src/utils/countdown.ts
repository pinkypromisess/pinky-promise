// Renders a Conversation's server-computed `expires_at` as a human "life
// remaining" countdown for CUJ #4 ("the conversation shows a visible 'life
// remaining' countdown"). `expires_at` is `null` once a Conversation is
// `pinky_promised` (no expiry).

export function formatExpiryCountdown(expiresAt: string | null, now: Date = new Date()): string {
  if (!expiresAt) return 'Pinky Promised — no expiry';

  const diffMs = new Date(expiresAt).getTime() - now.getTime();
  if (diffMs <= 0) return 'Expired';

  const days = Math.floor(diffMs / (1000 * 60 * 60 * 24));
  if (days >= 1) return `${days}d left`;

  const hours = Math.floor(diffMs / (1000 * 60 * 60));
  if (hours >= 1) return `${hours}h left`;

  const minutes = Math.max(1, Math.floor(diffMs / (1000 * 60)));
  return `${minutes}m left`;
}
