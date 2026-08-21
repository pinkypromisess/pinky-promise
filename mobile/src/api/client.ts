import { getAuthToken } from './authToken';
import type { ApiErrorBody } from './types';

const API_BASE_URL = process.env.EXPO_PUBLIC_API_BASE_URL ?? 'http://localhost:8080/v1';

export class ApiError extends Error {
  status: number;
  body: ApiErrorBody;

  constructor(status: number, body: ApiErrorBody) {
    super(body.message);
    this.status = status;
    this.body = body;
  }
}

interface RequestOptions {
  method: 'GET' | 'PUT' | 'PATCH' | 'POST' | 'DELETE';
  path: string;
  body?: unknown;
}

export async function apiRequest<T>({ method, path, body }: RequestOptions): Promise<T> {
  const token = await getAuthToken();

  const response = await fetch(`${API_BASE_URL}${path}`, {
    method,
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });

  if (response.status === 204) {
    return undefined as T;
  }

  const json = await response.json();

  if (!response.ok) {
    throw new ApiError(response.status, json as ApiErrorBody);
  }

  return json as T;
}
