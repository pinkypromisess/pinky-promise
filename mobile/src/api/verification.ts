import { apiRequest } from './client';
import type { Verification } from './types';

export function startVerification(): Promise<Verification> {
  return apiRequest<Verification>({ method: 'POST', path: '/verification' });
}

export function getVerificationStatus(): Promise<Verification> {
  return apiRequest<Verification>({ method: 'GET', path: '/verification/status' });
}
