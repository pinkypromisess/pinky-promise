import { formatExpiryCountdown } from '../src/utils/countdown';

describe('formatExpiryCountdown', () => {
  const now = new Date('2026-08-29T00:00:00.000Z');

  it('renders a no-expiry message when expires_at is null', () => {
    expect(formatExpiryCountdown(null, now)).toBe('Pinky Promised — no expiry');
  });

  it('renders days when more than a day remains', () => {
    const expiresAt = new Date(now.getTime() + 1000 * 60 * 60 * 24 * 2 + 1000 * 60 * 60 * 3).toISOString(); // 2d3h
    expect(formatExpiryCountdown(expiresAt, now)).toBe('2d left');
  });

  it('renders hours when less than a day but at least an hour remains', () => {
    const expiresAt = new Date(now.getTime() + 1000 * 60 * 60 * 11).toISOString(); // 11h
    expect(formatExpiryCountdown(expiresAt, now)).toBe('11h left');
  });

  it('renders minutes when less than an hour remains', () => {
    const expiresAt = new Date(now.getTime() + 1000 * 60 * 30).toISOString(); // 30m
    expect(formatExpiryCountdown(expiresAt, now)).toBe('30m left');
  });

  it('renders "Expired" once expires_at is in the past', () => {
    const expiresAt = new Date(now.getTime() - 1000 * 60).toISOString();
    expect(formatExpiryCountdown(expiresAt, now)).toBe('Expired');
  });
});
