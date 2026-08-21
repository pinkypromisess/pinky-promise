# Pinky Promise — Project Context

Dating app MVP. Full CUJs and entity/API design live in docs/pinky-promise-cujs.md
and docs/pinky-promise-entities-api.md — read these before writing code.

## Stack
- Backend: C++ (Drogon framework), Postgres (Cloud SQL), GCS for photo storage,
  deployed on Cloud Run
- Frontend: React Native + Expo
- Verification: AWS Rekognition (Face Liveness + CompareFaces), called from GCP

## Module boundaries
This repo is being built by multiple parallel agent sessions, each owning one
module (see docs/pinky-promise-entities-api.md for the module list). Do not
modify files outside your assigned module's scope. If you need a change to a
shared type or the API contract, stop and flag it rather than editing directly.

## Conventions
- API contract lives in backend/api/openapi.yaml — treat it as the source of
  truth for request/response shapes across modules.
- SQL migrations are plain .sql files in backend/migrations/, applied in order.

## Standing engineering requirements (apply to every module)
- Any endpoint claimed as tested must have at least one test that goes through
  the real HTTP router + auth filter + real DB — not just service-layer or
  pure-logic-level tests. State explicitly, per test, which layer it exercises.
- All SQL must use parameterized queries, never string concatenation.
- In async/callback code (Drogon handlers), never capture local variables or
  `this` by reference in a lambda that outlives the current stack frame — use
  capture-by-value or shared_ptr instead.
- Every module must have a GitHub Actions CI workflow that builds and runs its
  tests in a clean container. Before reporting a module "done," push and confirm
  the Actions run is actually green — do not report local test results as
  equivalent to CI passing.
- Never commit secrets, API keys, or credentials. Confirm .gitignore covers
  .env and any credential files before every push.
- Stay strictly within your assigned module's files. If you need a change to
  a shared contract (openapi.yaml, shared types), stop and flag it rather than
  editing it directly.
